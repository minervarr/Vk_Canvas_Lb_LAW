# USAGE_hdr_output.md — presenting above SDR white, on every platform

A design + work plan for letting a consumer opt into an HDR-capable swapchain
and a matching output colour transform. Written after the ViewMage question:
*"the decode keeps >1.0 linear alive to the GPU — why does nothing brighter
than SDR white ever reach the panel?"*

## Goal

A consumer that says "I have display-referred-to-scene content and a capable
panel" gets a swapchain whose colourspace the compositor understands as HDR,
and every pipeline in this engine writes pixels encoded correctly for it.
SDR consumers — including the camera preview that forced today's pin — see
**bit-identical** behaviour unless they ask otherwise.

Non-goals: Dolby Vision (`VK_COLOR_SPACE_DOLBYVISION_EXT` — licensing, ignore),
HLG as a *swapchain* target (scene-referred ambiguity; the camera already
handles HLG itself before we ever see it), Wayland HDR (backend is README-only;
colour-management protocol is its own future document), and any automatic
content sniffing — HDR is requested by the consumer, never guessed.

## Where we stand

| Fact | Where |
|---|---|
| `VK_EXT_swapchain_colorspace` is enabled at instance level when present | `renderer.cc:833-836`, flag `ext_swapchain_colorspace_` |
| Swapchain creation pins **R8G8B8A8_UNORM + SRGB_NONLINEAR**, unconditionally | `renderer.cc:942-944` |
| Members + a stale comment claim HDR resolution "when the surface supports it" | `renderer.hh:155-161` — the scaffolding survived; the selection code did not |
| Only knob exposed to consumers is image count | ctor `renderer.hh:29-30` |
| Render pass + views derive from `swapchain_format_`, so a format change propagates | `renderer.cc:1042`, `:1084` |
| Image pipeline: per-draw `exposure/toneMode/white/clipWarn`; final `saturate` + `srgbEncode` hard-coded | `texture.hh:45-68`, `image_frag.slang:115-116` |
| Vector/text/shapes write display-referred Colour directly — **no encode step anywhere else** | `shape_frag/overlay_frag/msdf_frag` |
| Push-constant block is 16 floats (64 B) and MUST stay character-identical vert↔frag | `image_frag.slang:3-18` |
| Readback assumes tightly packed RGBA8, PNG-ready | `renderer.cc:681`, used by `capture/capture.cc:111` |

The pin exists because one consumer presented HLG-encoded pixels into the only
10-bit colourspace a phone exposed (BT2020 *linear*) and the display read
non-linear data as linear — washed-out preview, correct diagnosis, wrong cure
scope (`renderer.cc:936-945`). The cure belongs behind a default-off switch,
not a hard-coded floor.

## Design

Two independent decisions, deliberately separated:

**1. OutputTarget — swapchain level, one per process.**
What surface we present into. Opt-in, capability-resolved, logged.

```
enum class OutputTarget { SdrSrgb /*default*/, ExtendedLinearScrgb, Hdr10PQ };
Renderer(SurfaceProvider&, AssetReader&, uint32_t images = 4,
         OutputTarget requested = OutputTarget::SdrSrgb);
bool hdrActive() const;          // did we actually get what was requested?
OutputTarget activeTarget() const;
```

Selection is **enumerate, prefer, fall back** — never assume a device exposes
anything:

| Requested | Preference order (first supported wins) | Fallback |
|---|---|---|
| `ExtendedLinearScrgb` | `R16G16B16A16_SFLOAT + EXTENDED_SRGB_LINEAR` → `RGBA16_SFLOAT + BT2020_LINEAR` | SDR pin |
| `Hdr10PQ` | `A2B10G10R10_UNORM_PACK32 + HDR10_ST2084` → `A2B10…_3PACK16_PASSTHROUGH` (log loudly if used) | SDR pin |

All candidates are checked against one `vkGetPhysicalDeviceSurfaceFormatsKHR`
enumeration. Log the **full** enumeration once at DEBUG level: we do not yet
know what real drivers expose (see Open questions) and this log is how we find
out.

**2. OUTPUT_ENCODE — pipeline level, baked per pipeline as a specialization
constant.**
How pixels are written into that surface. This is the part that is easy to get
wrong, because the encode is *not* an image-pipeline concern:

> Under an extended-linear target the whole framebuffer is linear light. The
> vector overlay, shapes, MSDF text and letterbox clear are authored as
> display-referred sRGB numbers. If they keep writing them raw into a linear
> surface, **all UI goes washed out** — exactly the camera bug again, from the
> other side.

So the encode cannot live only in `image_frag`. It becomes a specialization
constant (`OUTPUT_ENCODE ∈ {Srgb, LinearScRgb, PQ}`) fixed at pipeline
creation — pipelines already depend on `swapchain_format_`, so this adds zero
per-frame cost and zero push-constant churn. Every fragment stage ends with
one shared, included function:

- `Srgb`: current behaviour, byte-for-byte (encode + dither + saturate).
- `LinearScRgb`: `pow(c, 2.4)`-family inverse only — linearize authored UI
  colours; image path skips `saturate` (>1.0 is legal; 1.0 = SDR reference
  white ≈ 80 nits nominal, user-scalable by the OS).
- `PQ`: ST 2084 OETF after tone-mapping into `[0,1]` against target maxLuminance.

Per-draw tone controls (`exposure`, `white`) remain runtime uniforms — the
knee just rolls toward a display-relative white instead of 1.0, which the
consumer already passes. `clipWarn` stripes mean "beyond *this target's*
range", so its threshold derives from `OUTPUT_ENCODE`.

## Work items

| # | Item | Files | Size |
|---|---|---|---|
| 1 | Pure selector: `pickTarget(span<VkSurfaceFormatKHR>, bool ext, OutputTarget)` — no Vulkan calls inside | `core/output_target.{hh,cc}` new | ~60 L + ~120 L test |
| 2 | Unit test, assert-style like `layout_test` (runs desktop, <1 s): preference tables, missing-ext, empty-list, duplicate formats | `core/tests/output_target_test.cc` | — |
| 3 | Wire selector into `create_swapchain()` behind the ctor flag; fix stale comment `renderer.hh:155-157`; add `hdrActive()`/`activeTarget()` | `renderer.cc/hh` | ~30 L |
| 4 | Shared `shaders_src/output_encode.slang` (+ PQ OETF constants); `OUTPUT_ENCODE` spec-constant threaded through all pipeline creations | 6 vk_canvas shaders + font-engine's `msdf/composite` consumers note | ~80 L |
| 5 | `image_frag`: gate `saturate`/dither/`clipWarn` threshold on encode mode; **keep push-constant block character-identical** in `image_vert.slang` | `image_{frag,vert}.slang` | ~25 L |
| 6 | Optional `VK_EXT_hdr_metadata` → `vkSetHdrMetadataEXT` when PQ chosen (maxCLL from consumer's intensity_target) | `renderer.cc:893` area | ~20 L |
| 7 | Guard: `readbackLastFrame` asserts SDR target (PNG contract); capture path documents/pins SDR | `renderer.cc:681`, `capture/*` | ~10 L |
| 8 | Consumer-side contract written down (cannot live here — core has no Java): Android requires `Window.setColorMode(COLOR_MODE_HDR)` from the app's Activity; desktop requires OS HDR enabled | this file §Consumer contract | docs |

## Platform reality

**Windows (desktop today):** `EXTENDED_SRGB_LINEAR` + FP16 is the canonical
path (mirrors DXGI `RGB_FULL_G10_NONE_P709`). Needs "HDR" toggled in Windows
display settings — absent that, formats won't advertise and we fall back.
`VK_EXT_swapchain_colorspace` reaches us through whatever ICD the loader has;
the existing has-check covers it.

**Android:** driver-dependent, genuinely. Modern panels commonly expose either
the FP16 extended-linear pair or A2B10 + `HDR10_ST2084`. Samsung/Pixel differ;
that is why item 1 logs everything. `COLOR_MODE_HDR` on the window is required
for some compositors to treat our SurfaceView as HDR — verify empirically per
device, do not assume.

**Linux/Wayland:** out of scope until the backend exists; note left in
`platform/linux/README.md` pointer-wise only.

## Testing honestly

- Selector + preference logic: unit-tested on desktop (item 2).
- PQ constants: golden-value test against known ST.2084 pairs in C++ (the
  shader mirrors them; the shader itself stays untested — say so).
- Everything visual: verified on a real HDR panel (S23 Ultra class; Windows
  monitor with HDR on). No automated nit measurement exists here, and
  `readbackLastFrame` PNGs cannot prove luminance. Regression proof for the
  camera consumer is *absence of change*: default-off, bit-identical swapchain
  params asserted in the unit test.
- Same honesty rules as always: claims limited to what was actually run.

## Open questions (need one real device session each)

1. What does an S23 Ultra's Android Vulkan driver actually enumerate? (Item 3's log answers.)
2. Does `setColorMode(COLOR_MODE_HDR)` change the enumerated format list on Samsung? (Re-enumerate after.)
3. Does FP16-extended exist across Samsung's driver generations, or is PQ the safe Android pick?

## Rollout order (party rules)

1. Land items 1–7 here, default-off, via `git_wrapper` from inside this repo.
2. Consumers bump their pin. First customer (ViewMage): ctor flag +
   `setColorMode(COLOR_MODE_HDR)` in its own Activity + feed intensity_target
   as display-relative white. Three lines, then device-verify.
