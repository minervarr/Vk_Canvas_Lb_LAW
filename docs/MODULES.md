# Module index

Status: canonical, 2026-10-31. This is the audit index: what exists, what it
depends on, what tests it, and whether it is compiled. Update it whenever a file
moves, a target is split, or a test is added.

Current shape: **39 compiled `.cc` files** — 23 in the five engine module
directories (`base/`, `input/`, `layout/`, `draw/`, `gpu/`) and 16 in the `ui/`
toolkit. Engine targets: `vk_canvas_base` (base + input + layout),
`vk_canvas_draw`, `vk_canvas_gpu`; the toolkit is `vk_canvas_ui`.
`vk_canvas_core` remains the source-compatible umbrella (an INTERFACE target
linking all four for one transition cycle). `tools/check_deps.py` checks the
per-target source inventory, the layer edges between directories, and the
platform/WIP/UI include rules. Release module objects carry function/data
sections and consumers link with section GC (`cmake/VceModules.cmake`), so an
unused function inside a pulled object is dropped as well.

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
| GPU orchestration | `gpu/renderer.hh`, `gpu/renderer_impl.hh`, `gpu/renderer.cc`, `gpu/renderer_api.cc`, `gpu/renderer_{device,swapchain,camera,text,text_pipeline,shapes,readback}.cc` | ~3,050 | platform, canvas, overlay, image_layer, output_target, vk_check | `gpu_smoke_test`, `renderer_lifecycle_test`, probes | **Split.** Public header is a pimpl; largest renderer TU 632 lines. Checked calls, device/surface-loss state. |
| checked calls | `gpu/vk_check.hh` | ~85 | Vulkan headers | `vk_result_test` | Abort/throw/return policies plus the VkResult classifier; no device needed. |
| overlay rasterizer | `gpu/overlay.*` | ~800 | platform, output_target, vk_check | probes | Compute tiling/coverage path; large resources lazy; init reports shader failure. Largest engine TU (677 lines) but one subsystem. |
| image layer | `gpu/image_layer.*` | ~670 | platform, texture, output_target, vk_check | probes | Texture upload/draw/reaper; upload failures clean up after themselves. |
| output target | `gpu/output_target.*` | ~400 | stdlib only | `output_target_test` | Vulkan-free format/colour-space resolution; the Vulkan adapter is a guarded compatibility overload. |
| headless | `gpu/headless.*` | ~70 | platform | probes, `renderer_lifecycle_test` | `VK_EXT_headless_surface` provider for capture/tests; `set_extent()` drives resize/minimize. |
| art texture | `gpu/art_texture.*` | ~51 | platform, texture, img_decode | none yet | Read file → decode → texture. Candidate for a CPU/GPU integration test. |
| capture | `capture/*` | ~194 | core, Wayland | none | Dev tool, not part of the library. |

## `ui/` toolkit

Built as `vk_canvas_ui`, which links `vk_canvas_draw` and `vk_canvas_base`.
Its sources and headers include no Vulkan and no `gpu/` header; the final link
still carries libvulkan because the font-engine submodule's archive does.
`vk_canvas_core` still links it for the transition cycle; new consumers link
`vk_canvas_ui` explicitly.

| Module | Files | Lines | Depends on | Tests | Notes |
|---|---|---|---|---|---|
| widget contract | `ui/widgets.hh` | 353 | canvas, frame_input | all widget tests | Public structs, styles, geometry/draw declarations. |
| widget helpers | `ui/widgets_internal.hh`, `ui/widgets_text_fit.cc` | ~45 | canvas, text_util | indirectly | Shared `vcenter`/`drawLabelFit` and the opt-in `applyTextFit` policy. |
| text field | `ui/widgets_text_field.cc` | 295 | canvas, keys | `ui_state_test` | UTF-8-safe editing, click mapping, draw. |
| toggle | `ui/widgets_toggle.cc` | 27 | canvas | `widget_geometry_test` | Switch row. |
| radio | `ui/widgets_radio.cc` | 47 | canvas | none yet (draw path) | Content-fitted pill, dot row. |
| stepper | `ui/widgets_stepper.cc` | 35 | canvas | `widget_geometry_test` | − value + cluster. |
| slider | `ui/widgets_slider.cc` | 57 | canvas | `widget_geometry_test` | Geometry + value mapping. |
| segmented | `ui/widgets_segmented.cc` | 35 | canvas | `widget_geometry_test` | N-way exclusive row. |
| dropdown | `ui/widgets_dropdown.cc` | 43 | canvas | none yet (draw path) | Closed field + chevron. |
| scroll list | `ui/widgets_scroll_list.cc` | 68 | canvas | `widget_geometry_test` (height only) | Windowed rows, pill/underline styles. |
| table | `ui/widgets_table.cc` | 121 | canvas | `widget_geometry_test` (header) | Sortable header, windowed body. |
| button/group | `ui/widgets_button.cc`, `ui/widgets_group_header.cc` | ~61 | canvas, text_util | none yet (draw path) | Fit/two-line button; section label. |
| text area | `ui/textarea.*` | ~253 | canvas, font | none yet | Scrollable, wrapped text view. |
| plot view | `ui/plotview.*` | ~345 | canvas | none yet | Plot widget. |
| pager | `ui/pager.*`, `ui/pager_draw.cc` | ~163 | types; draw half uses Canvas | `pager_test` | Page model tested without Canvas; `drawDots()` is separate. |
| theme | `ui/theme/theme.hh` | ~70 | base types, type scale | `ui_state_test` | Colour roles, spacing/stroke/density tokens; `ui::TypeScale` aliases the base ladder. |

Splitting `widgets.cc` into one TU per widget is what makes the one-widget
`size_ui_toggle` probe 32.8 KB smaller than `size_ui_all_widgets`
(`docs/PERFORMANCE.md`); it also gives each widget an independently auditable
file. The font engine, not widget code, dominates any text-drawing binary.

`core/tests/` holds 11 unit binaries plus 4 GPU tests (`text_edge_probe`,
`shape_outline_probe`, `gpu_smoke_test`, `renderer_lifecycle_test`);
`ui/tests/` holds 3 unit binaries (`pager_test`, `widget_geometry_test`,
`ui_state_test`). All 18 pass on this machine as of 2026-10-31, and the whole
set is leak-free under ASan+UBSan. The text tests use the canonical
`assets/fonts` face when checked in, otherwise the small face in the font-engine
submodule; with neither they skip (exit 77). Shaders compile to SPIR-V 1.3
through glslang because the renderer asks for a Vulkan 1.1 device.

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
| Windows | `platform/windows/*` | Win32 + `Build.bat`; compiles shaders via `VceShaders.cmake` |
| Android | `platform/android/*` | Gradle + NDK; camera/HWB path lives in `core/gpu/renderer_camera.cc` behind `__ANDROID__` |

## App_shell

`App_shell` is a separate repository. Portable target: `ui_metrics`,
`ui_orientation`, `utf16_utf8`, `app_paths` (desktop). Hosts: Win32, Wayland,
Android. The four CPU-only tests are wired through `App_shell/dev/` and all pass
under `ctest` as of 2026-10-31; `App_shell/dev/fuzz/utf16_fuzz.cc` is the
libFuzzer harness for the JNI UTF-16 seam. `ui_metrics` now includes only the
base-layer `type_scale.hh`; `computeUiMetrics(shortSide, minReadablePx)` takes
the app's font-derived floor, so no generated header is required.

## Updating this index

- Add a row when a file or module is added.
- Move a row to `wip/` when a prototype is quarantined.
- Name the test binary that actually covers the module; "none yet" is honest
  and visible.
- Keep the compiled-source count in sync with `tools/check_deps.py`.
