# Testing

Status: canonical, 2026-10-31. The convention is inherited from the rest of the
family: plain `assert()`, no test framework, CTest as the discovery/runner.

## One command

```bash
Vk_Canvas_Lb_LAW/tools/audit.sh
```

It runs the dependency/source-inventory check, configures and builds the Vk dev
tree, builds the header self-containment target, runs every unit test, then the
Phase 3 stages: validation-layer GPU runs, ASan+UBSan CTest, soak repeats and
libFuzzer. It does the same build/test side for `App_shell` when the two
repositories are checked out together. Build trees default to
`/tmp/opencode/{vk-dev,app-shell-dev}` (plus `-validation`, `-sanitize`, `-fuzz`);
override with `VK_BUILD_DIR` / `APP_SHELL_BUILD_DIR` / `VK_VALIDATION_DIR` /
`VK_SANITIZE_DIR` / `VK_FUZZ_DIR`. Build trees are reused, never deleted.

Stage switches: `VK_AUDIT_VALIDATION=0`, `VK_AUDIT_SANITIZE=0`, `VK_AUDIT_SOAK=0`,
`VK_AUDIT_FUZZ_SECONDS=0`. A stage that cannot run prints why and is skipped;
it is never counted as a pass.

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
| `fuzz` | UTF-8/UTF-16, text buffer, HTML sidecar parser | clang + libFuzzer (`dev/fuzz/`) | crash = bug |

The current tree has 14 Vk `unit` tests and 4 App_shell tests. The GPU tests
(`text_edge_probe`, `shape_outline_probe`, `gpu_smoke_test`,
`renderer_lifecycle_test`) are registered with the `gpu` label and
`SKIP_RETURN_CODE 77`; the dev superbuild compiles the Slang shaders into
`<build>/assets/shaders` when `slangc` is found (override with
`-DVCE_SLANGC=/path/to/slangc` or disable with
`-DVCE_DEV_BUILD_SHADERS=OFF`). Shaders are emitted as SPIR-V 1.3 through
glslang, because the renderer asks for a Vulkan 1.1 instance/device. The text
tests use the canonical `assets/fonts` face when checked in, otherwise the face
in the font-engine submodule; with neither they skip (77).

## Current unit tests

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
| `pager_test` | `ui/pager.*` | clamp, settle, fling, page rects, zero viewport |
| `widget_geometry_test` | `ui/widgets.*` geometry | toggle/stepper/slider/segment/table rects |
| `ui_state_test` | `ui/widgets.*` text state + theme | insertion, deletion, selection, IME, type scale |
| `canvas_records_test` | `canvas.*` | curve records, SDF packing, images, clipping |
| `owner_semantics_test` | `Renderer`/`OverlayRasterizer`/`ImageLayer` | compile-time non-copyability |
| `vk_result_test` | `vk_check.hh` | VkResult → swapchain/recovery classification |
| `renderer_lifecycle_test` | `Renderer` | resize, minimize/restore, surface loss/regain, create/destroy soak (GPU) |

## Adding a test

1. Put it in `Vk_Canvas_Lb_LAW/core/tests/<module>_test.cc`,
   `Vk_Canvas_Lb_LAW/ui/tests/<module>_test.cc`, or `App_shell/tests/`.
   One binary per module or tightly-related group.
2. Start with `#undef NDEBUG` so `assert()` survives a Release build.
3. Include only the module under test where possible. Pure tests compile the
   module `.cc` directly; they do not link `vk_canvas_core` unless the module's
   own symbols need the font/renderer half.
4. Register it in `core/CMakeLists.txt` with `vce_add_pure_test` (engine) or in
   `ui/CMakeLists.txt` with `vce_add_ui_test` (toolkit), passing the
   module-relative source path:

   ```cmake
   vce_add_pure_test(my_module_test base/my_module.cc)
   vce_add_ui_test(my_widget_test)
   ```

   The helpers add the include roots, C++17, warnings, the `unit` label and
   `add_test` when `BUILD_TESTING` is on. UI tests link `vk_canvas_ui`, which
   links draw+font but never Vulkan.
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

## Fuzzing

The parsers that consume untrusted bytes have libFuzzer harnesses under
`dev/fuzz/`:

```bash
cmake -S Vk_Canvas_Lb_LAW/dev -B /tmp/opencode/vk-fuzz -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++ -DVCE_DEV_BUILD_FUZZ=ON \
    -DVCE_DEV_BUILD_SHADERS=OFF -DVCE_DEV_BUILD_PLATFORM=OFF
cmake --build /tmp/opencode/vk-fuzz --target utf8_fuzz text_buffer_fuzz html_strip_fuzz
/tmp/opencode/vk-fuzz/fuzz/utf8_fuzz -max_total_time=30
```

`utf8_fuzz` pins forward progress in the shared decoder, `text_buffer_fuzz`
drives the gap buffer through arbitrary edit/cursor sequences and checks the
line/cursor invariants after every step, and `html_strip_fuzz` exercises the
HTML-sidecar flattener. The JNI seam's other direction has its own harness in
`App_shell/dev/fuzz/utf16_fuzz.cc` (`-DAPP_SHELL_DEV_BUILD_FUZZ=ON`):
`utf16::to_utf8` / `to_utf16` over arbitrary units and bytes, including
unpaired surrogates and cursor positions past the end. `tools/audit.sh` runs
each for `VK_AUDIT_FUZZ_SECONDS` (default 5) when clang is available. The
`wip/archive` prototype is not fuzzed because it is not compiled and has no
target.

## What is not tested yet

- Real `VK_ERROR_DEVICE_LOST`: the state machine is unit-tested, but the event
  itself is not injectable without `VK_EXT_device_fault` or hardware removal.
- `OverlayRasterizer` and `ImageLayer` resource ownership under allocation
  failure; the sanitizer and lifecycle soak cover the normal teardown path.
- `textarea`, `plotview` and `art_texture`; widget geometry and text-field
  state are covered, but their draw calls are not pixel-tested.
- Platform hosts (Win32/Wayland/Android) and the Java half.
- Exact pixel output across drivers; the GPU tests assert structure, not hashes.
- `App_shell`'s `Host`/`AppView` integration and Android JNI paths.

These gaps are Phase 4/5 work. Until then, changes to those areas need a written
manual verification note in the commit or PR, and must not claim automated
coverage.

## Limitations

- The dev tree compiles the SPIR-V assets with `slangc`; all three GPU tests
  run on this machine, including MSDF via the font-engine submodule face. A
  checkout without that submodule skips the text tests; record a skip instead
  of treating it as a pass.
- No CI workflow is checked in yet. `tools/audit.sh` is the intended CI entry
  point; adding the workflow is part of the foundation work.
- A sanitizer build is `-DVCE_DEV_SANITIZE=ON` in the dev tree; the audit script
  runs it. There is still no benchmark target (`docs/PERFORMANCE.md` records the
  baseline); Phase 4 adds budgets and a regression check.
