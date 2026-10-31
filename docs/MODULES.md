# Module index

Status: canonical, 2026-10-31. This is the audit index: what exists, what it
depends on, what tests it, and whether it is compiled. Update it whenever a file
moves, a target is split, or a test is added.

Current shape: **19 compiled `.cc` files, ~9,530 lines** across five physical
module directories — `base/`, `input/`, `layout/`, `draw/`, `gpu/` — built as
three static targets: `vk_canvas_base` (base + input + layout),
`vk_canvas_draw` and `vk_canvas_gpu`. `vk_canvas_core` remains the
source-compatible umbrella (an INTERFACE target linking all three).
`tools/check_deps.py` checks the per-target source inventory, the layer edges
between directories, and the platform/WIP/UI include rules.

## Compiled engine

Files are module-relative. `base/`, `input/` and `layout/` compile into
`vk_canvas_base`; `draw/` into `vk_canvas_draw`; `gpu/` into `vk_canvas_gpu`.

| Module | Files | Lines | Depends on | Tests | Notes |
|---|---|---|---|---|---|
| value types | `base/types.hh`, `base/texture.hh` | ~97 | stdlib only | header check | `Rect`, `Color`, texture handle/format. Must stay Vulkan-free. |
| logging | `base/log.hh` | ~30 | stdlib, guarded `android/log.h` | header check | Allowlisted platform shim. |
| platform seam | `gpu/platform.hh` | ~36 | Vulkan, font `asset_reader.hh` | header check | The only core header with platform surface types; implemented by the backends. |
| input | `input/input.hh`, `input/keys.hh`, `input/frame_input.*`, `input/gesture.*` | ~398 | types/stdlib | `frame_input_test`, `gesture_test` | `FrameInput` is the per-frame edge detector; `GestureRecognizer` is the touch classifier. |
| layout/animation | `layout/layout.*`, `base/animated_float.*` | ~163 | types/stdlib | `layout_test`, `animated_float_test` | Pure math: scale, dock, cursors, interpolation. |
| text data | `base/text_buffer.*`, `base/undo_redo.*` | ~360 | stdlib | `text_buffer_test`, `undo_redo_test` | Gap buffer + edit history. No Canvas, no font, no Vulkan. |
| draw | `draw/canvas.*` | ~1,015 | types, texture, font types | `canvas_records_test` | Emits curve records or SDF shape quads. The test inspects the floats directly. |
| text helpers | `draw/text_util.*`, `draw/responsive_text.hh`, `draw/text_cover.hh` | ~300 | Canvas/types | indirectly via `text_cover_test`; `text_util` needs a font | Truncate/wrap/center, text-size formula, linear-light coverage math. |
| GPU orchestration | `gpu/renderer.*` | ~3,470 | platform, canvas, overlay, image_layer, output_target | probes (skip without assets) | **Monolith.** 2,849-line `.cc`, 60+ members. T2.2 pimpl + split. |
| overlay rasterizer | `gpu/overlay.*` | ~762 | platform, output_target | probes | Compute tiling/coverage path; large resources lazy. |
| image layer | `gpu/image_layer.*` | ~641 | platform, texture, output_target | probes | Texture upload/draw/reaper. |
| output target | `gpu/output_target.*` | ~299 | Vulkan types | `output_target_test` | Pure format/colour-space resolution. |
| headless | `gpu/headless.*` | ~64 | platform | probes | `VK_EXT_headless_surface` provider for capture/tests. |
| art texture | `gpu/art_texture.*` | ~51 | platform, texture, img_decode | none yet | Read file → decode → texture. Candidate for a CPU/GPU integration test. |
| widgets | `draw/widgets.*` | ~1,083 | canvas, frame_input | none yet (geometry helpers are pure) | **UI layer.** Phase 2 moves to `ui/widgets`. Split before moving. |
| text area | `draw/textarea.*` | ~253 | canvas, font | none yet | **UI layer.** Scrollable, wrapped text view. |
| plot view | `draw/plotview.*` | ~345 | canvas | none yet | **UI layer.** Plot widget. |
| pager | `draw/pager.*`, `draw/pager_draw.cc` | ~163 | types; draw half uses Canvas | `pager_test` | Page model is tested without Canvas; `drawDots()` is separate. |
| capture | `capture/*` | ~194 | core, Wayland | none | Dev tool, not part of the library. |

`core/tests/` is ~1,713 lines across 11 unit binaries plus 2 GPU probes. All 11
unit tests pass under `ctest` on Linux as of 2026-10-31.

## Not compiled: `wip/`

~2,508 lines. No target, no supported API. Details and porting notes are in
[`../wip/README.md`](../wip/README.md).

| File | Status |
|---|---|
| `canvas_host.*` | Android-only prototype; subsumed by `Renderer` + `SurfaceProvider`. |
| `vulkan_state.*` | Older Android state owner; references removed font-engine headers. |
| `compute_context.*` | Unused compute helper; timestamp idea belongs in the renderer split. |
| `text_editor.*` | Android editor prototype; build on `text_buffer`/`undo_redo` in `ui/`. |
| `font_weight.hh` | Used only by the editor prototype. |
| `archive.*` | Portable persistence prototype; unused by the engine. Decide with a consumer. |

## Platform backends

| Backend | Files | Status |
|---|---|---|
| Linux/Wayland | `platform/linux/*` | builds `vk_canvas_wayland`; no shader compilation in this target |
| Windows | `platform/windows/*` | Win32 + `Build.bat`; compiles shaders via `VfeShaders.cmake` |
| Android | `platform/android/*` | Gradle + NDK; camera/HWB path lives in `renderer.cc` behind `__ANDROID__` |

## App_shell

`App_shell` is a separate repository. Portable target: `ui_metrics`,
`ui_orientation`, `utf16_utf8`, `app_paths` (desktop). Hosts: Win32, Wayland,
Android. The four CPU-only tests are wired through `App_shell/dev/` and all pass
under `ctest` as of 2026-10-31. `ui_metrics` currently depends on
`layout.hh` and a build-generated `ui_min_text_size.gen.h`; Phase 2 moves the
type scale to `ui/theme` and removes the generated-header coupling.

## Updating this index

- Add a row when a file or module is added.
- Move a row to `wip/` when a prototype is quarantined.
- Name the test binary that actually covers the module; "none yet" is honest
  and visible.
- Keep the compiled-source count in sync with `tools/check_deps.py`.
