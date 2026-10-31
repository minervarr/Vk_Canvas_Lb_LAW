# wip — unported prototypes

These files are kept for their design and history. They are **not compiled**,
not part of any CMake target, and not a supported API. They were moved out of
`core/` so that directory contains only code that builds and is tested.

| File | What it was | Why it is here | What porting would require |
|---|---|---|---|
| `canvas_host.*` | A camera-free Vulkan host that presented a finished `Canvas` curve buffer | Android-only includes (`android/native_window.h`, `android/asset_manager.h`) and no longer matches the `core/gpu/platform.hh` seam | Rebuild on `Renderer`/`SurfaceProvider`; it is largely subsumed by `Renderer` + `HeadlessSurfaceProvider` |
| `vulkan_state.*` | Older Android Vulkan state owner | Includes Android SDK headers and font-engine internals (`curve_rasterizer.hh`, `msdf_renderer.hh`) that no longer exist | Retire in favour of `Renderer`; no direct port |
| `compute_context.*` | Standalone compute-pass helper with GPU timestamp support | Unused by the current renderer; the timestamp idea lives in the renderer/perf work | Wire into `core/gpu/` once the renderer split defines the seam |
| `text_editor.*` | Android text-editor widget prototype | Depends on `platform/android/input_handler.hh`, `vulkan_state.hh` and a `TextFont`; not portable | Build on `text_buffer` + `undo_redo` + `Canvas`/`FrameInput` in `ui/widgets/` |
| `font_weight.hh` | Font-weight enum used by `text_editor`/`vulkan_state` | Only those prototypes include it | Keep with the editor prototype, or move to `ui/theme` when the editor is ported |
| `archive.*` | Generic `RecordStore`/`KeyValueStore` over `std::fstream` | Portable but unused by the engine; persistence belongs to an app or App_shell | Add tests and a consumer first, then decide whether it is a `core/data` module or an App_shell utility |

No file here should be added to `core/CMakeLists.txt` without tests. When one is
ported, move it back with `git mv`, add it to a module target, add its test, and
update `docs/MODULES.md` and `docs/ROADMAP.md`.

These files still carry the AGPLv3 header where present; the repository license
applies.
