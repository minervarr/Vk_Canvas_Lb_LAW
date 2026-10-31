# Module index

Status: canonical, 2026-10-31. This is the audit index: what exists, what it
depends on, what tests it, and whether it is compiled. Update it whenever a file
moves, a target is split, or a test is added.

Current shape: **19 compiled `.cc` files, ~9,530 lines** in `core/`, plus
platform backends. Modules are logical today; Phase 1 turns them into physical
directories and CMake targets. `tools/check_deps.py` checks the compiled-source
inventory and the layer rules.

## Compiled engine

| Module | Files | Lines | Depends on | Tests | Notes |
|---|---|---|---|---|---|
| value types | `types.hh`, `texture.hh` | ~97 | stdlib only | header check | `Rect`, `Color`, texture handle/format. Must stay Vulkan-free. |
| logging/seam | `log.hh`, `platform.hh` | ~66 | Vulkan, font `asset_reader.hh` | header check | `log.hh` has a guarded `android/log.h`; allowlisted. `platform.hh` is the only core header with platform surface types. |
| input | `input.hh`, `keys.hh`, `frame_input.*`, `gesture.*` | ~398 | types/stdlib | `frame_input_test`, `gesture_test` | `FrameInput` is the per-frame edge detector; `GestureRecognizer` is the touch classifier. |
| layout/animation | `layout.*`, `animated_float.*` | ~163 | types/stdlib | `layout_test`, `animated_float_test` | Pure math: scale, dock, cursors, interpolation. |
| text data | `text_buffer.*`, `undo_redo.*` | ~360 | stdlib | `text_buffer_test`, `undo_redo_test` | Gap buffer + edit history. No Canvas, no font, no Vulkan. |
| draw | `canvas.*` | ~1,015 | types, texture, font types | `canvas_records_test` | Emits curve records or SDF shape quads. The test inspects the floats directly. |
| text helpers | `text_util.*`, `responsive_text.hh`, `text_cover.hh` | ~300 | Canvas/types | indirectly via `text_cover_test`; `text_util` needs a font | Truncate/wrap/center, text-size formula, linear-light coverage math. |
| GPU orchestration | `renderer.*` | ~3,470 | platform, canvas, overlay, image_layer, output_target | probes (skip without assets) | **Monolith.** 2,849-line `.cc`, 60+ members. Phase 1 pimpl + split. |
| overlay rasterizer | `overlay.*` | ~762 | platform, output_target | probes | Compute tiling/coverage path; large resources lazy. |
| image layer | `image_layer.*` | ~641 | platform, texture, output_target | probes | Texture upload/draw/reaper. |
| output target | `output_target.*` | ~299 | Vulkan types | `output_target_test` | Pure format/colour-space resolution. |
| headless | `headless.*` | ~64 | platform | probes | `VK_EXT_headless_surface` provider for capture/tests. |
| art texture | `art_texture.*` | ~51 | platform, texture, img_decode | none yet | Read file → decode → texture. Candidate for a CPU/GPU integration test. |
| widgets | `widgets.*` | ~1,083 | canvas, frame_input | none yet (geometry helpers are pure) | **UI layer.** Phase 2 moves to `ui/widgets`. Split before moving. |
| text area | `textarea.*` | ~253 | canvas, font | none yet | **UI layer.** Scrollable, wrapped text view. |
| plot view | `plotview.*` | ~345 | canvas | none yet | **UI layer.** Plot widget. |
| pager | `pager.*`, `pager_draw.cc` | ~163 | types; draw half uses Canvas | `pager_test` | Page model is tested without Canvas; `drawDots()` is separate. |
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
