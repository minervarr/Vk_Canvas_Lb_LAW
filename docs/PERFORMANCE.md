# Performance baseline

Status: third baseline after Phase 4, 2026-10-31. Phase 0 and Phase 1 numbers
remain below as historical references. Phase 4 adds the per-feature binary-size
table, its budgets and the LTO measurement; frame numbers are still smoke
measurements, not frame-rate claims.

## Reference machine

| | |
|---|---|
| Date | 2026-10-31 |
| OS | Linux 7.2.11-arch1-1 x86_64 |
| CPU | Intel Core i7-11800H @ 2.30 GHz, 16 threads |
| GPU | Intel TigerLake-H UHD Graphics + NVIDIA RTX 3050 Mobile (GA107M) |
| Vulkan ICDs present | `intel_icd`, `intel_hasvk_icd`, `nouveau_icd` |
| Vulkan loader | 1.4.363 |
| Compiler | GCC 16.2.1 20260810 |
| CMake / Ninja | 4.4.4 / 1.13.2 |
| Build flags | Debug: `-g -O2` for the core target; Release: CMake Release |
| Build trees | `/tmp/opencode/vk-baseline-{debug,release}`, `/tmp/opencode/app-shell-release` |

This is a laptop with hybrid graphics. Treat absolute numbers as a local
reference, not a hardware-independent claim. Record the machine with every
future baseline.

**Decision (2026-10-31):** this machine is the reference for size and timing
budgets. Any CI uses lavapipe for deterministic correctness runs only; it is not
a timing or size baseline, and no timing gate belongs in CI until a real
benchmark scene exists.

## Build and test

| Measurement | Result |
|---|---|
| Vk dev configure (Debug) | 0.49 s |
| Vk dev build (Debug, cold) | 10.6 s |
| Vk unit tests (11, Debug) | 0.02 s total |
| App_shell dev configure (Release) | 1.26 s |
| App_shell build (Release, cold) | 4.5 s |
| App_shell unit tests (4, Debug) | 0.00 s total |

Commands:

```bash
cmake -S Vk_Canvas_Lb_LAW/dev -B /tmp/opencode/vk-baseline-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/opencode/vk-baseline-debug
ctest --test-dir /tmp/opencode/vk-baseline-debug --output-on-failure
```

## Static library and minimal-link size

| Artifact | Size |
|---|---|
| `libvk_canvas_core.a`, Debug | 4,866,844 bytes |
| `libvk_canvas_core.a`, Release | 386,596 bytes |
| — Release `.text` / `.data` / `.bss` | 185,709 / 296 / 0 bytes |
| `libapp_shell.a`, Release | 22,460 bytes (text 8,506) |
| Minimal layout-only executable, stripped | 14,472 bytes |
| Minimal Canvas + font executable, stripped | 952,888 bytes |

The two minimal executables answer the "unused code" question concretely:

- The layout-only binary references `dockTop`, so the linker pulls only
  `layout.o` from the archive: ~14 KB including startup.
- The Canvas binary pulls `canvas.o`, which references the font engine's text
  helpers, so the font engine comes with it: ~953 KB stripped.

This is the object-file granularity effect described in the roadmap, not a
runtime cost. Phase 4 did the split and section GC; the measured result is in
"Phase 4 pay-for-use baseline" below (the Canvas floor drops from ~953 KB to
~23 KB).

## Phase 1 re-baseline (2026-10-31)

The umbrella is now an INTERFACE target, so there is no single
`libvk_canvas_core.a`. The release archives are measured per module.

| Measurement | Result |
|---|---|
| Dev configure, Release, no platform/capture | 0.39 s |
| Dev build, Release cold, no platform/capture | 9.3 s |
| `libvk_canvas_base.a`, Release | 42,202 bytes (text 16,810 / data 128) |
| `libvk_canvas_draw.a`, Release | 162,410 bytes (text 89,278 / data 16) |
| `libvk_canvas_gpu.a`, Release | 217,866 bytes (text 84,227 / data 200) |
| Module archives, total | 422,478 bytes (text 190,315 / data 344) |
| Minimal layout-only executable, stripped | 14,472 bytes |
| Minimal Canvas + font executable, stripped | 952,888 bytes |
| Full CTest run, Debug (14 tests) | 0.24 s; 11 unit + 3 gpu, all pass |

The minimal binaries are unchanged from Phase 0, which is the useful result:
splitting the renderer did not change what a small consumer pays. The module
archive total is ~9% larger than the old single archive for the same code; that
is archive bookkeeping, not runtime cost, and Phase 4 measures the linked
result.

Commands:

```bash
cmake -S Vk_Canvas_Lb_LAW/dev -B /tmp/opencode/vk-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DVCE_DEV_BUILD_PLATFORM=OFF \
      -DVCE_DEV_BUILD_CAPTURE=OFF
cmake --build /tmp/opencode/vk-release
# minimal layout consumer
g++ -std=c++17 -O2 -Icore/layout -Icore/base <main.cc> \
    /tmp/opencode/vk-release/core/libvk_canvas_base.a -o min_layout
```


## Phase 4 pay-for-use baseline (2026-10-31)

### Build policy

Release-like configurations (`Release`, `RelWithDebInfo`, `MinSizeRel`) now
compile the module targets with `-ffunction-sections -fdata-sections` and link
consumers with `-Wl,--gc-sections` (MSVC `/Gy` + `/OPT:REF`, Apple
`-Wl,-dead_strip`). The policy lives in `cmake/VceModules.cmake`; the option
`VCE_CANVAS_FUNCTION_SECTIONS=OFF` reproduces the old behaviour for the
comparison below. Debug builds stay section-free.

`tools/size_report.sh` builds `dev/size/` twice (GC on/off) and checks
`tools/size_budgets.txt`. The probes are link-only consumers and never run:

| Probe | References |
|---|---|
| `size_layout` | `dockTop` only (`vk_canvas_base`) |
| `size_canvas_rect` | `Canvas` constructor + `rect()` (`vk_canvas_draw`) |
| `size_ui_geometry` | widget geometry helpers only, no Canvas (`vk_canvas_ui`) |
| `size_ui_toggle` | one drawn widget (`vk_canvas_ui`) |
| `size_ui_all_widgets` | every drawn widget (`vk_canvas_ui`) |
| `size_gpu_renderer` | `Renderer::draw` forwarder (`vk_canvas_gpu`) |

### Linked, stripped feature probes

Archives are static; the executable is the honest app-facing number.

| Probe | GC off | GC on (shipped) | GC saves | GC-on `.text` |
|---|---:|---:|---:|---:|
| `size_layout` | 14,472 | 14,464 | 8 | 1,633 |
| `size_canvas_rect` | 952,888 | **22,864** | 930,024 | 15,082 |
| `size_ui_geometry` | 981,592 | **14,592** | 967,000 | 4,143 |
| `size_ui_toggle` | 952,888 | **936,488** | 16,400 | 893,994 |
| `size_ui_all_widgets` | 989,792 | **969,288** | 20,504 | 927,557 |
| `size_gpu_renderer` | 126,296 | **48,040** | 78,256 | 41,462 |

Module archives grow slightly with section GC (more section headers); that is
archive bookkeeping, not app cost:

| Archive | GC off | GC on | GC-on `.text` |
|---|---:|---:|---:|
| `libvk_canvas_base.a` | 44,164 | 55,212 | 247 |
| `libvk_canvas_draw.a` | 76,150 | 91,270 | 27,492 |
| `libvk_canvas_gpu.a` | 285,552 | 319,848 | 9,351 |
| `libvk_canvas_ui.a` | 113,646 | 127,494 | 526 |

What these numbers answer:

- **A primitive-only Canvas app pays ~23 KB, not ~953 KB.** Without section GC
  the linker extracts the whole `canvas.o`, whose unused text methods reference
  the font engine. With GC those sections are dropped before archive extraction,
  so the font engine is linked only when text is actually referenced.
- **Pure widget geometry costs ~14.6 KB.** Drawing text is what costs: the
  one-widget probe is 936 KB, i.e. ~921 KB of font engine, not widget code.
- **One widget no longer pays for every widget.** `size_ui_all_widgets` minus
  `size_ui_toggle` is 32,800 bytes with GC (36,904 without). `ui/widgets.cc` was
  split into one TU per widget, so the toggle probe keeps
  `widgets_toggle.o` + shared helpers instead of a 730-line object.
- **The Renderer entry point costs ~48 KB** plus the shared Vulkan loader.
  Its per-subsystem TUs are already the right granularity; no further split is
  justified by these measurements.

Source, compile time and audit surface are paid regardless of what the linker
keeps; this table measures only the binary an app ships.

### Budgets

`tools/size_budgets.txt` holds the gates for the GC-on column (stripped bytes),
with roughly 10% headroom over the measurement above:

| Probe | Budget |
|---|---:|
| `size_layout` | 16,000 |
| `size_canvas_rect` | 26,000 |
| `size_ui_geometry` | 16,000 |
| `size_ui_toggle` | 1,040,000 |
| `size_ui_all_widgets` | 1,080,000 |
| `size_gpu_renderer` | 55,000 |

`tools/audit.sh` runs the report and fails on a budget overrun. These are
per-machine: re-baseline deliberately when the compiler, libc or font-engine
submodule changes; `VK_SIZE_BUDGET=0` reports without gating.

### LTO (optional)

`VCE_CANVAS_LTO=ON` (or the dev tree's `VCE_DEV_LTO=ON`) enables CMake IPO on the
module targets and the targets built with them. A consumer must also enable IPO
on its final executable so the link runs through the LTO plugin.

Measured on the reference machine, same sources:

| Measurement | LTO off | LTO on |
|---|---:|---:|
| Release build, 174 targets incl. size probes (cold) | 9,704 ms | 10,121 ms (+4.3%) |
| Debug dev tree, 249 targets (cold) | 11,944 ms | 11,818 ms (noise) |
| `size_ui_toggle`, stripped | 936,488 | 932,392 (−0.4%) |
| `size_ui_all_widgets`, stripped | 969,288 | 961,104 (−0.8%) |
| `size_canvas_rect` / `size_layout` / `size_gpu_renderer` | 22,864 / 14,464 / 48,040 | unchanged (±8) |
| `gpu_smoke_test` CPU total, median of 5 | 0.059 ms | 0.060 ms |
| `gpu_smoke_test` fence wait / record, median | 0.002 / 0.003 ms | 0.002 / 0.004 ms |

The tiny scene in `gpu_smoke_test` cannot show a frame-time win, and the
sub-1% binary improvement does not pay for a 4% Release build-time increase.
LTO stays opt-in; it needs a real scene measurement before anyone claims it.

Commands:

```bash
Vk_Canvas_Lb_LAW/tools/size_report.sh          # table + budget gate
VK_SIZE_BUDGET=0 Vk_Canvas_Lb_LAW/tools/size_report.sh   # report only

cmake -S Vk_Canvas_Lb_LAW/dev -B /tmp/opencode/vk-lto -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DVCE_DEV_LTO=ON \
      -DVCE_DEV_BUILD_SIZE_PROBES=ON -DVCE_DEV_BUILD_SHADERS=OFF \
      -DVCE_DEV_BUILD_PLATFORM=OFF -DVCE_DEV_BUILD_CAPTURE=OFF
cmake --build /tmp/opencode/vk-lto
```

## GPU/frame timings

Measured after Phase 1: the dev tree compiles the SPIR-V assets (see
`VCE_DEV_BUILD_SHADERS`), `VK_EXT_headless_surface` is available, and the
selected device is the Intel UHD iGPU.

| Test | Result |
|---|---|
| `shape_outline_probe` | pass, 0.07 s wall (includes Renderer init) |
| `text_edge_probe` | pass, using the font-engine submodule face |
| `gpu_smoke_test` | pass, 6 frames; last frame CPU total 0.054 ms, fence wait 0.003 ms, record 0.004 ms |

`gpu_smoke_test` covers overlay curves, shapes, images, clipping, rotation,
MSDF and FrameStats, so no draw path is skipped. These are smoke numbers, not a
frame-rate claim: the frames are tiny (192x128) and the scene is one primitive
per frame.

Validation is clean on this configuration: Phase 3 pinned SPIR-V 1.3 for the
Vulkan 1.1 instance and fixed the two other findings the validation layer
exposed. `tools/audit.sh` fails on any `VUID-` or `Validation Error` in the four
GPU binaries; see [`RELIABILITY.md`](RELIABILITY.md).

To measure a real scene later:

1. Build with `-DVCE_DEV_BUILD_SHADERS=ON` (default) and a reachable `slangc`.
2. Run with `-DVCE_VK_VALIDATION=ON` while bringing up a path; the audit's
   validation stage is the same check and must stay clean.
3. Add a benchmark scene (shape count, glyph count, images, resolution) and
   use `Renderer::lastFrameStats()` plus GPU timestamps per pass.
4. Record the GPU/ICD used. Driver choice changes the result; never compare
   timings across ICDs silently.

## Allocation and steady-state goals

Not measured yet. The intended target is zero heap allocations in the
steady-state UI frame build (`Canvas` + widgets) after warm-up, and stable RSS
over a long run. The lifecycle soak exists; a counting-`operator new` test does
not, so the allocation target is still a design goal, not a measured result.

## Targets and status

- 60 fps at 1080p with 2,000 SDF shapes + 5,000 glyphs on integrated graphics —
  **not measured**; needs a benchmark scene.
- CPU frame build under 1 ms for a typical screen — **not measured** for a real
  screen (the 192×128 smoke frame is 0.054 ms and is not representative).
- Zero steady-state heap allocations in the UI frame build — **not measured**;
  needs a counting allocator.
- Cold start to first frame under 100 ms on desktop (excluding asset decode) —
  **not measured**.
- Memory stable over 10,000 frames; no Vulkan object leaks at teardown — leak
  checks pass under ASan; a 10,000-frame RSS run is not recorded.
- Minimal UI binary under 1 MB stripped after module pruning — **met**:
  primitive-only Canvas 22,864 B; one drawn widget 936,488 B including the font
  engine; all widgets 969,288 B.
- Zero validation-layer errors in Debug — **met**: the validation stage is a
  gate and reports zero issues on the four GPU binaries.
- Zero unchecked Vulkan calls in Release — **not mechanically verified**: the
  three-policy rule is applied by review (ADR 0003), and no scanner proves its
  absence.

## What to record for every future measurement

- Commit hash and build type.
- Machine/CPU/GPU/ICD and driver version.
- Compiler and flags.
- Scene: shape count, glyph count, images, overlay-vs-shape path, resolution.
- CPU build, CPU submit, GPU per-pass, frame interval, RSS/VRAM, binary size.
- Whether the run was headless or on a real surface.
