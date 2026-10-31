# Testing

Status: canonical, 2026-10-31. The convention is inherited from the rest of the
family: plain `assert()`, no test framework, CTest as the discovery/runner.

## One command

```bash
Vk_Canvas_Lb_LAW/tools/audit.sh
```

It runs the dependency/source-inventory check, configures and builds the Vk dev
tree, builds the header self-containment target, runs every unit test, and does
the same for `App_shell` when the two repositories are checked out together.
Build trees default to `/tmp/opencode/{vk-dev,app-shell-dev}`; override with
`VK_BUILD_DIR` / `APP_SHELL_BUILD_DIR`. Build trees are reused, never deleted.

Run a single side manually:

```bash
cmake -S Vk_Canvas_Lb_LAW/dev -B /tmp/opencode/vk-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/opencode/vk-dev
ctest --test-dir /tmp/opencode/vk-dev --output-on-failure
ctest --test-dir /tmp/opencode/vk-dev -R layout_test --output-on-failure
```

```bash
cmake -S App_shell/dev -B /tmp/opencode/app-shell-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/opencode/app-shell-dev
ctest --test-dir /tmp/opencode/app-shell-dev --output-on-failure
```

## Tiers

| Label | What | Needs | Failure policy |
|---|---|---|---|
| `unit` | pure logic, CPU only, no GPU/swapchain | a compiler; some tests include Vulkan headers | must pass |
| `gpu` | headless surface, real swapchain, pixel probes | Vulkan device + built SPIR-V assets | skip `77` if unavailable |
| `bench` | frame/startup/allocation/size | as above; planned | report, not a hard gate yet |
| `fuzz` | UTF-8/UTF-16, text buffer, archive parsers | sanitizer build; planned | crash = bug |

The current tree has 11 Vk `unit` tests and 4 App_shell tests. The GPU tests
(`text_edge_probe`, `shape_outline_probe`, `gpu_smoke_test`) are registered with
the `gpu` label and `SKIP_RETURN_CODE 77`; the dev superbuild compiles the
Slang shaders into `<build>/assets/shaders` when `slangc` is found (override
with `-DVCE_SLANGC=/path/to/slangc` or disable with
`-DVCE_DEV_BUILD_SHADERS=OFF`). `text_edge_probe` still skips without a font at
`assets/fonts/newcomputermodern/NewCM10-Regular.otf`.

## Current Vk unit tests

| Test | Module | What it pins |
|---|---|---|
| `text_cover_test` | `text_cover.hh` | sRGB vs linear-light text coverage; the white-ring regression |
| `layout_test` | `layout.*` | UiScale floor/uncapped scale, dock, cursors, scroll clamp |
| `animated_float_test` | `animated_float.*` | easing, overshoot clamp, restart |
| `frame_input_test` | `frame_input.*` | edge detection, wheel accumulation, key queue |
| `output_target_test` | `output_target.*` | HDR/SDR format resolution and fallbacks |
| `gesture_test` | `gesture.*` | tap/pan/fling/cancel/long-press ordering |
| `text_buffer_test` | `text_buffer.*` | gap buffer, cursor, line indexing, out-of-range views |
| `undo_redo_test` | `undo_redo.*` | insert/erase undo/redo, coalescing, redo invalidation |
| `pager_test` | `pager.*` | clamp, settle, fling, page rects, zero viewport |
| `canvas_records_test` | `canvas.*` | curve records, SDF packing, images, clipping |
| `owner_semantics_test` | `Renderer`/`OverlayRasterizer`/`ImageLayer` | compile-time non-copyability |

## Adding a test

1. Put it in `Vk_Canvas_Lb_LAW/core/tests/<module>_test.cc` (or
   `App_shell/tests/`). One binary per module or tightly-related group.
2. Start with `#undef NDEBUG` so `assert()` survives a Release build.
3. Include only the module under test where possible. Pure tests compile the
   module `.cc` directly; they do not link `vk_canvas_core` unless the module's
   own symbols need the font/renderer half.
4. Register it in `core/CMakeLists.txt` with the `vce_add_pure_test` helper,
   passing the module-relative source path:

   ```cmake
   vce_add_pure_test(my_module_test base/my_module.cc)
   ```

   The helper adds the module include roots, C++17, warnings, the `unit` label
   and `add_test` when `BUILD_TESTING` is on.
5. If the module has a Canvas half, split it (see `pager.cc` /
   `pager_draw.cc`) instead of dragging Canvas into a logic test.
6. Run `tools/audit.sh`; update `docs/MODULES.md` with the new test.

## Testing patterns

- **Record emitters.** `Canvas` appends plain floats; `canvas_records_test`
  asserts the record type, centre, half extents, colour, bounding box and the
  1px tiling margin. Use this instead of a GPU when the question is geometry or
  clipping.
- **Static owner rules.** `owner_semantics_test` uses `static_assert` on
  `std::is_copy_constructible_v`; a compile failure is the test.
- **Skip vs fail.** A GPU test that cannot create a headless surface, find a
  shader, or load a font exits `77`, and CTest treats it as skipped. A test that
  can run must fail on a real regression, not silently skip.
- **Goldens.** Prefer structural pixel assertions (background, known rect
  centres, ink coverage) over an exact hash across drivers. Use lavapipe for a
  deterministic run when available.

## What is not tested yet

- `Renderer` lifecycle edge cases: swapchain recreation, surface loss, device
  loss. The normal draw paths (overlay, shapes, images, clip, rotation) are in
  `gpu_smoke_test`.
- `OverlayRasterizer` and `ImageLayer` resource ownership/teardown under stress.
- `widgets`, `textarea`, `plotview`, `art_texture`.
- Platform hosts (Win32/Wayland/Android) and the Java half.
- Exact pixel output across drivers; the GPU tests assert structure, not hashes.
- `App_shell`'s `Host`/`AppView` integration and Android JNI paths.

These gaps are Phase 2/3 work. Until then, changes to those areas need a written
manual verification note in the commit or PR, and must not claim automated
coverage.

## Limitations

- The dev tree now compiles the SPIR-V assets with `slangc`; the shape and
  smoke tests run on this machine. `text_edge_probe` and the MSDF section of
  `gpu_smoke_test` still skip when no font is checked in. Record a skip instead
  of treating it as a pass.
- No CI workflow is checked in yet. `tools/audit.sh` is the intended CI entry
  point; adding the workflow is part of the foundation work.
- There is no benchmark or sanitizer build configuration yet; Phase 3/4 add
  them.
