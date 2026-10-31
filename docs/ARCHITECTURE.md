# Architecture

Status: canonical, 2026-10-31. Deep engine notes remain in `CLAUDE.md`; this
file is the map and the rules, not the full reference.

## Layers and dependency direction

```
consumer app
   |
   +-- app_shell          program lifecycle, platform hosts, paths, input
   |
   +-- ui/                theme, type hierarchy, layout, widgets, UI state
          |
          +-- core/      engine: draw, text, input, GPU, data
                 |
                 +-- first_party/  font engine, image decode
```

| Layer | May depend on | Must never depend on |
|---|---|---|
| `core/base` | C++ stdlib | Vulkan, platform SDKs, app concepts |
| `core/input` | base | UI, GPU |
| `core/draw` | base, font types | GPU, platform SDKs |
| `core/gpu` | base, draw, platform seam | UI, app concepts |
| `ui/` | base, input, draw | Vulkan, platform SDKs, `Renderer` |
| `app_shell` portable | base (generic types only) | UI, GPU |
| platform hosts | gpu, platform SDKs | UI policy |

The current tree is flatter than this target. Phase 1 makes the module targets
real; the rules apply now and are checked by `tools/check_deps.py`. See
[`ROADMAP.md`](ROADMAP.md) for the migration.

## Repository and build topology

- `Vk_Canvas_Lb_LAW` has **no root CMakeLists.txt**. `core/CMakeLists.txt`,
  `platform/windows/CMakeLists.txt`, `platform/linux/CMakeLists.txt` and
  `platform/android/` own their builds. Consumers add `core/` plus exactly one
  backend.
- `dev/CMakeLists.txt` is a development superbuild: it adds `core`, the Linux
  backend, capture and the tests so one configure/build/test covers the tree.
  It is not a library entry point.
- `App_shell` is a separate repository. Its portable target links nothing;
  each host links one platform backend. It must not learn application concepts.
- Shaders are Slang, compiled per platform by the consuming build. The Linux
  backend currently builds the window/protocol layer, not the shaders; probes
  skip (`exit 77`) when the SPIR-V assets are absent.
- `vulkan_font_engine` is a submodule with nested FreeType/msdfgen submodules.
  `img_decode_kit` is checked in. Both are independent of `vk_canvas` at the
  include level; `art_texture.cc` is the thin Vulkan-aware wrapper.

## Runtime frame flow

1. The host owns the platform window and a `Renderer`. It implements the
   `SurfaceProvider`, `AssetReader` and `FrameWaker` seams from
   `core/platform.hh`.
2. The application builds a frame into reusable buffers: curve records
   (`Canvas` default path), SDF shape vertices (`Canvas::useShapes`), MSDF text
   quads, background/foreground `ImageDraw` lists.
3. `Renderer::draw()` composites, in order: camera/external image, background
   images, SDF shapes, the curve overlay (compute tiling + coverage), foreground
   images, then MSDF text. The sRGB text pass, when active, is a second render
   pass so the blend reads the page in linear light.
4. Two frames are in flight. The curve-overlay path is single-buffered and
   serializes against the previous frame; shape-path hosts get full CPU/GPU
   overlap. The compute rasterizer's large resources are allocated lazily on
   the first non-empty upload, so a shapes-only host never pays for them.
5. `Renderer::recreate_surface()` handles a window that dies and comes back
   under a live process without rebuilding the device, pipelines or textures.
   It reports incompatibility (format/colour-space change) instead of guessing.

## Ownership and lifetime

- The type that creates a Vulkan handle destroys it. `Renderer` owns the
  device, swapchain, pipelines and atlases; it exposes borrowed handles
  (`vkDevice()`, `vkQueue()`, `msdfAtlasImage()`) for a consumer that needs to
  write the atlas itself.
- `OverlayRasterizer` and `ImageLayer` are renderer-internal owners with
  explicit `cleanup()`. Copy construction/assignment is deleted on all three;
  a copy would double-free. `owner_semantics_test` pins this at compile time.
- `Canvas` owns nothing on the GPU. It appends to caller-owned vectors; the
  caller clears/reuses them once per frame.
- `AssetReader`/`SurfaceProvider`/`FrameWaker` are borrowed by reference for
  the renderer's lifetime. The host outlives the renderer.
- `wip/` is not part of any target and has no lifetime guarantees.

## Error handling

Current state is mixed: construction paths throw `std::runtime_error` for
fatal Vulkan failures, while some `vkCreate*` results are unchecked. The target
policy is:

- Every Vulkan call that can fail is checked with file/line/result context.
- Construction fails loudly and leaves no half-initialized owner.
- `cleanup()` is idempotent and safe after a partial init.
- Recoverable conditions (out-of-date/suboptimal swapchain, surface lost,
  device lost) are reported through a return value or status, not an exception
  from a destructor or a hot-path call.
- No `vkQueueWaitIdle` in a per-frame path unless the caller explicitly asked
  for it.

Phase 3 applies the policy; the roadmap tracks it.

## Threading contract

- `Renderer::draw()`, swapchain recreation and texture upload are render-thread
  operations.
- `Renderer::update_camera_frame()` may be called from another thread; the
  Android HWB path is guarded by `hwb_mutex_` and the pending frame is consumed
  on the render thread.
- `FrameWaker::wake()` may be called from any thread; the platform
  implementation owns its synchronization.
- `Renderer::frameReady()` / the atomic wake flag are the cross-thread signal.
  Nothing else is implicitly thread-safe.
- `App_shell` hosts own their event queues and document their own locking.

## Public-header boundaries

- `core/canvas.hh` and `core/types.hh` are draw-level headers and must not pull
  Vulkan or platform SDKs.
- `core/renderer.hh` is a GPU-level header; it exposes Vulkan types and the
  platform seam. `ui/` must not include it.
- `core/platform.hh` is the only core header allowed to depend on Vulkan's
  platform surface types; it is implemented by the backends.
- `App_shell/host.hh` and `app_view.hh` are the program seam; they use portable
  `LayoutRect`/`ColorRef`, not Win32 types.

## Known deviations (tracked in the roadmap)

- `core/renderer.cc` is a 2,849-line monolith; the public header exposes 60+
  members. Phase 1 pimpls and splits it.
- `core/log.hh` and `core/renderer.hh` contain guarded platform includes (the
  logging shim and the Android camera path). They are allowlisted in
  `check_deps.py` and scheduled to move behind the seam.
- `core/` still mixes widgets and generic logic; Phase 2 moves the widget layer
  to `ui/`.
- `App_shell/ui_metrics.hh` includes a build-generated header; Phase 2 moves
  the type scale to `ui/theme` and passes the floor in as an app value.
