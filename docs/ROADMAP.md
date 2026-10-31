# Roadmap — auditable, test-driven, high-performance canvas framework

Status: living document. Last updated: 2026-10-31.
Supersedes the initial workspace draft kept outside the repository.

This file is the durable memory for the framework direction. Update the phase
checklist and the "Current position" block whenever work advances, and keep the
workspace resume state in `/home/nerio/.opencode/{status,context,todo}.md` in
sync.

---

## Current position

- **Phase:** 3 — reliability hardening (DONE)
- **Last completed:** Phase 3 — checked Vulkan calls and symmetric
  init/teardown, device/surface-loss state, `vk_result_test`, the headless
  lifecycle test, validation/sanitizer/soak stages in `tools/audit.sh`,
  libFuzzer harnesses, and `docs/RELIABILITY.md`.
- **Next action:** start Phase 4 at T5.1 in `.opencode/todo.md`: Release
  function/data sections with `--gc-sections`, then measure per-feature binary
  size and link the results into `docs/PERFORMANCE.md`.
- **Blocking decisions:** none. Reference hardware is recorded in
  `docs/PERFORMANCE.md`; GPU smoke/probes run on this machine when the dev
  tree builds SPIR-V assets (`slangc` on PATH or `-DVCE_SLANGC=...`).
- **Resume phrase:** "continue next phase" means start Phase 4 (pay-for-use and
  performance budgets) at T5.1 in `.opencode/todo.md`. Phase 3 is green.

### How to resume after a context cleanup

```bash
cd /home/nerio/code/minervarr/pureFrameworkLibs
cat .opencode/status.md        # current goal, next action, evidence
cat .opencode/todo.md          # checked/unchecked work
cat Vk_Canvas_Lb_LAW/docs/ROADMAP.md
```

Phase 0, Phase 1, Phase 2 and Phase 3 are green: the engine modules, the
`Renderer` pimpl/split, GPU smoke coverage, the Vulkan-free `output_target`
adapter, the `ui/` toolkit boundary, and the checked-call/recovery/audit
hardening are all in place and audited. Phase 4 (pay-for-use and performance
budgets) is next.

---

## 1. Vision

Two libraries, one goal: let an application be written once and run as a real
program on Windows, Linux/Wayland and Android.

- `vk_canvas` answers **how does it look**: a Vulkan canvas, text, images,
  shapes, layout and input primitives.
- `app_shell` answers **how does it run**: window, event loop, input, paths,
  orientation, crash handling, platform lifecycle.

The next layer, `ui/`, answers **how does an app compose a screen quickly**:
theme, type hierarchy, spacing, widgets, focus and state. It must not be
required to use the engine, and the engine must not depend on it.

The framework is not a product by itself. Its consumers are apps such as
Matrix Player, UI Demo and VideoPlayer. API decisions favour the next consumer,
not the current demo or the first app.

## 2. Quality goals (non-negotiables)

1. **Auditable from zero.** A new engineer or AI can start at `docs/` and trace
   any feature to its module, public contract, test and owner without tribal
   knowledge.
2. **Test-driven.** Pure logic runs as unit tests anywhere; GPU paths have
   headless probes; every bug fix starts with a failing test. Refactors are
   behaviour-preserving and proven by tests, not by inspection.
3. **Remarkable performance, measured.** No performance claim without a
   baseline. Steady-state UI must not allocate per frame. The renderer keeps
   its fast paths: SDF shapes for primitives, MSDF for text, lazy compute
   rasterizer, two frames in flight where possible.
4. **Stability and reliability by construction.** Owned Vulkan objects have
   explicit ownership, checked results, validation-clean teardown, and tested
   recovery paths (resize, minimize, surface loss, device loss, HDR downgrade).
5. **Pay for what you use.** Unused subsystems must not tax the app's binary,
   startup, memory or build time. This is an engineering property, not a code
   generator trick.
6. **One canonical place per fact.** Code comments hold invariants and
   contracts; rationale and history live in `docs/decisions/`. `AGENTS.md`,
   `CLAUDE.md` and the docs must not contradict each other.

Non-goals for this phase:

- A QML-style runtime UI language.
- A source generator that rewrites or forks framework internals per app.
- Rewriting the public API wholesale for style. Correctness, boundaries and
  testability come first; naming/namespace cleanup is a later, mechanical pass.
- Retaining every historical behaviour that blocks a clean boundary. Old
  behaviour can be kept behind a compatibility shim for one release, then
  removed deliberately.

## 3. Decisions locked (2026-10-31)

1. **First slice:** docs + test harness + WIP/type cleanup. No renderer split or
   `ui/` move until the safety net is green.
2. **Compatibility:** keep source compatibility for existing consumers. Keep
   include names and the `vk_canvas_core` umbrella working; add
   `vk_canvas_ui` as a new target. `vk_canvas_core` may include UI for one
   transition cycle; `vk_canvas_engine` is the precise base+draw+gpu target.
3. **`ui/` location:** inside `Vk_Canvas_Lb_LAW`, built as `vk_canvas_ui`.
   Extract to a repository only if it gains independent consumers.
4. **Test style:** plain `assert()` with CTest discovery; no test framework. A
   tiny helper header is allowed, not a framework.
5. **`archive`:** move to `wip/` for now; revisit when a consumer needs generic
   persistence. Do not compile it into the engine without tests.
6. **Reference hardware/CI:** still open. The baseline doc must name the
   machine, compiler, GPU/ICD and whether lavapipe is acceptable for GPU CI.

## 4. Target architecture

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

Dependency rules:

| Layer | May depend on | Must never depend on |
|---|---|---|
| `core/base` | C++ stdlib | Vulkan, platform SDKs, app concepts |
| `core/input` | base | UI, GPU |
| `core/draw` | base, font types | GPU, platform SDKs |
| `core/gpu` | base, draw, platform seam | UI, app concepts |
| `ui/` | base, input, draw | Vulkan, platform SDKs, `Renderer` |
| `app_shell` portable | base (only generic types) | UI, GPU |
| platform hosts | gpu, platform SDKs | UI policy |

`ui/` is the toolkit. It draws through `Canvas` and reads `FrameInput`; it does
not own a window, a device or a swapchain. That is what makes it testable on
CPU and usable in any app shell.

Module map:

```
core/
  base/        types (Rect, Color, TextureHandle), log, animation,
               text_buffer, undo_redo
  input/       input.hh, keys.hh, frame_input, gesture
  text/        text_cover, responsive_text
  draw/        canvas, text_util
  layout/      layout (dock/row/column/scale)
  gpu/         renderer (+ split), overlay, image_layer, output_target,
               headless, art_texture, platform.hh
  capture/     headless capture tool
  tests/       unit + probe tests
ui/
  theme/       colour roles, type roles, spacing, stroke, density
  layout/      rows/columns/grid/scroll helpers built on core/layout
  widgets/     button, toggle, radio, stepper, slider, list, table, text field
  navigation/  pager, focus order
  text/        hierarchy helpers, wrapping/ellipsis policy
  tests/       CPU-only tests
wip/           unported prototypes, clearly not part of any target
```

Notes:

- Physical directories are meaningful only where they enforce a boundary.
  `core/` vs `ui/` vs `wip/` is real. Splitting `core/gpu` into ten folders
  would not be.
- Include names stay stable where possible (`#include "canvas.hh"`) by
  propagating each module's include directory through its CMake target. The
  target is the contract; the path is an implementation detail.
- CMake targets mirror modules: `vk_canvas_base`, `vk_canvas_draw`,
  `vk_canvas_gpu`, `vk_canvas_ui`, plus the compatibility umbrella
  `vk_canvas_core` (base + draw + gpu, and `vk_canvas_ui` for one transition
  cycle).
- One file = one subsystem, so the linker's object-file granularity matches
  the auditor's mental model. This is the main "pay for what you use" lever and
  it costs no runtime performance.

Renderer split target (after pimpl):

```
gpu/renderer.hh            small public interface + unique_ptr<Impl> (public surface)
gpu/renderer.cc            Impl ctor/dtor, draw(), cleanup
gpu/renderer_api.cc        public Renderer forwarders (the only Impl accessor)
gpu/renderer_impl.hh       private state shared by the TUs below
gpu/renderer_device.cc     instance, surface, physical/logical device, queue, caps
gpu/renderer_swapchain.cc  swapchain, render passes, framebuffers, sync, text composite
gpu/renderer_camera.cc     external images, AHB/YCbCr, colour description (Android)
gpu/renderer_text.cc       MSDF/MTSDF atlas, text VBO, atlas updates
gpu/renderer_text_pipeline.cc  text pipeline, record, cleanup
gpu/renderer_shapes.cc     SDF shape VBO/pipeline
gpu/renderer_readback.cc   readback
gpu/vk_check.hh            VCE_VK_CHECK macro for fatal init paths
```

Exit criteria: no file over ~600 lines; public header no longer includes
`overlay.hh`, `image_layer.hh` or the font atlas internals; `Renderer` is
non-copyable/movable through its pimpl; all probes green. **Met 2026-10-31:**
largest file 534 lines, 14 tests pass (3 GPU, including MSDF via the
font-engine submodule face).

## 5. Current state and audit findings (2026-10-31)

Strengths already present:

- The platform seam (`platform.hh`) is real and small.
- The renderer has genuine fast paths and lazy allocation in the right places.
- Pure modules (`layout`, `animated_float`, `frame_input`, `output_target`,
  `text_cover`, `gesture`, `pager`, `text_buffer`, `undo_redo`) have no Vulkan
  or platform dependency and are testable.
- `app_shell` already splits portable code from the three hosts.
- The code is comment-rich and records hard-won driver/platform knowledge.
- The five pure tests pass today (`layout`, `animated_float`, `frame_input`,
  `output_target`, `text_cover`).

Problems that make auditing and testing harder:

| # | Finding | Status |
|---|---|---|
| 1 | No CTest harness; only `text_cover_test` is wired on Linux; the rest are manual `g++` commands. `App_shell` tests need a consumer and a generated header. | done (Phase 0) |
| 2 | `core/gpu/renderer.cc` is 2,849 lines; `renderer.hh` exposes 60+ members and mixes device, swapchain, Android camera, MSDF, shapes and frame logic. One object file pulls all of it. | done (Phase 1: pimpl + 9 TUs) |
| 3 | `core/` contains uncompiled WIP files (`canvas_host`, `vulkan_state`, `compute_context`, `text_editor`, `archive`). Two include Android SDK headers, violating the core rule. | done (Phase 0) |
| 4 | `layout.hh` and `pager.hh` include `canvas.hh` only for `Rect`/`Color`, dragging the draw API into pure geometry. | done (Phase 0) |
| 5 | `Renderer`, `OverlayRasterizer`, `ImageLayer` and `ComputeContext` own Vulkan handles but are implicitly copyable: a copy is a double-free. | done (Phase 0) |
| 6 | Init paths mix exceptions with unchecked `vkCreate*` calls. A failure can leave a half-initialized renderer. | done (Phase 3) |
| 7 | No performance or binary-size baseline; no harness, no budget, no regression check. | done (Phase 0 baseline; budgets in Phase 4) |
| 8 | Long historical narratives live in code comments; rationale should move to ADRs, code should state invariants. | open (Phase 3) |
| 9 | `App_shell/ui_metrics` is a UI design system depending on a build-generated header; it belongs in `ui/theme` and blocks standalone builds. | done (Phase 2) |
| 10 | No dependency enforcement: core can include a platform header, or UI can include Vulkan, and nothing fails. | done (T2.1) |

## 6. Roadmap

### Phase 0 — Foundation and test harness (DONE)

- [x] Canonical roadmap and resume state
- [x] `docs/` skeleton: `VISION.md`, `ARCHITECTURE.md`, `MODULES.md`,
      `TESTING.md`, `PERFORMANCE.md`, `THIRD_PARTY.md`
- [x] `dev/CMakeLists.txt` superbuild + `include(CTest)`; all existing pure
      tests discovered by CTest
- [x] App_shell test harness: dev-only stub for `ui_min_text_size.gen.h`,
      guarded `generate_ui_min_text_size` dependency
- [x] `tools/check_deps.py`: layer rules + compiled-source inventory
- [x] Header self-containment check
- [x] `tools/audit.sh`: configure, build, test, deps, headers, size report
- [x] Missing unit tests: `gesture`, `pager`, `text_buffer`, `undo_redo`,
      `canvas_records`
- [x] Split `Pager::drawDots` into `pager_draw.cc` (logic vs draw)
- [x] Move WIP to `wip/` with a README; remove Android SDK includes from
      `core/`
- [x] Extract `core/base/types.hh` (`Rect`, `Color`); break `layout.hh` and
      `pager.hh` from `canvas.hh`
- [x] Delete copy semantics on Vulkan owners; add static-assert test.
      Copying an owner was never a supported operation; this turns a latent
      double-free into a compile error.
- [x] Baseline: machine, compiler, test runtime, static-lib and minimal-link
      binary size, frame timings if headless is available

Exit: `ctest` green on Linux; `tools/audit.sh` runs; docs match the tree; no
behaviour change in any compiled module. **Verified 2026-10-31:** 11 Vk unit
tests + 4 App_shell tests pass; `audit: ok`; baseline in
`docs/PERFORMANCE.md`.

### Phase 1 — Module boundaries and renderer split (DONE)

- [x] Create the module CMake targets (`vk_canvas_base`, `vk_canvas_draw`,
      `vk_canvas_gpu`, compatibility `vk_canvas_core`) and move the compiled
      files with `git mv`.
- [x] Keep include names stable through each target's PUBLIC include directory.
      `App_shell` keeps its portable "links nothing" property: it carries the
      five module include roots itself instead of linking an engine target.
- [x] Map directories to modules in `tools/check_deps.py` and enforce the
      base/input/layout -> draw -> gpu edges plus the platform/WIP/UI rules.
- [x] Pimpl `Renderer`; split `renderer.cc` along device/swapchain/camera/text/
      shapes/readback boundaries. Largest TU is 534 lines.
- [x] Decouple `output_target.hh` from `vulkan.h` with the local
      `SurfaceFormat`/`OutputFormat` description; the Vulkan compatibility
      overload is available after vulkan.h and the renderer converts at the
      swapchain boundary.
- [x] Add a headless smoke test for every draw path (shapes, MSDF, images,
      overlay curves, clipping, rotation) and a `FrameStats` smoke test. MSDF
      skips until a face is present; the other paths run.
- [x] Add `VCE_VK_CHECK` and an opt-in validation-layer Debug configuration
      (`-DVCE_VK_VALIDATION=ON`).

Exit: files under budget; public surface smaller; tests green; a minimal
consumer links only the modules it uses.

### Phase 2 — `ui/` boundary (DONE)

- [x] Create the `ui/` target (`vk_canvas_ui`); move `widgets`, `textarea`,
      `plotview` and `pager` out of `core/draw`.
- [x] Keep old include paths working through the compatibility umbrella for
      one cycle. `vk_canvas_core` links `vk_canvas_ui`; new code links
      `vk_canvas_ui` explicitly.
- [x] Add CPU-only widget geometry tests (pure geometry) and a text-field
      UI-state test; both link `vk_canvas_ui`, not the renderer.
- [x] Add `ui/theme`: colour roles, type roles, spacing/stroke/density tokens.
- [x] Move the type-scale logic out of `app_shell/ui_metrics` into the base
      layer (`core/layout/type_scale.hh`); `ui/theme` re-exports it as
      `ui::TypeScale`. App_shell takes `minReadablePx` as an argument and
      builds without a generated header.

Exit: `core/` contains no widgets; `ui/` source and headers have no Vulkan
dependency; App_shell portable builds without a generated header. **Met
2026-10-31:** 16 CTest entries (13 unit + 3 gpu), `audit: ok`;
`check_deps.py` enforces the UI inventory and the no-GPU include rule. The
font-engine archive still puts libvulkan on the final link line; that is a
transitive property of the submodule's monolithic target, not a UI include.

### Phase 3 — Reliability hardening (DONE)

- [x] Apply checked Vulkan calls everywhere; make partial init and teardown
      symmetrical. Three policies in `gpu/vk_check.hh`; the constructor catches,
      cleans up and rethrows; init failures are reported instead of silently
      disabling a layer.
- [x] Add validation, sanitizer and soak runs to the audit script. Validation
      gating also found and fixed real defects: Slang's default SPIR-V 1.5 was
      invalid on the 1.1 device, unrequested `shaderDrawParameters`, and the
      capture path reusing an image index across a swapchain rebuild.
- [x] Add surface-loss/resize/minimize tests and a device-loss error path.
      `renderer_lifecycle_test` (headless), `Renderer::deviceLost()` /
      `surfaceLost()`, `vk_result_test`.
- [x] Add fuzz targets (UTF-8/UTF-16, text buffer, HTML sidecar parser).
      `dev/fuzz/` in both repositories; built only with clang + libFuzzer.
      Archive stays quarantined in `wip/` (not compiled, no target).
- [x] Document the platform contract and the thread contract in
      `docs/RELIABILITY.md`.

Exit: validation-clean, leak-free, fuzz-clean for covered parsers; recovery
paths exercised. **Verified 2026-10-31:** 18/18 CTest entries; validation zero
issues on all four GPU binaries; ASan+UBSan full CTest leak-free; soak 5/5;
three fuzz targets clean; `audit: ok`.

### Phase 4 — Pay-for-use and performance budgets (PLANNED)

- Enable function/data sections and GC sections in Release; measure.
- Split remaining monolithic TUs so archive granularity is fine.
- Add optional module switches only where measurements justify them.
- Add LTO as an option; measure compile time and frame time.
- Turn bench numbers into budgets with a loose CI regression threshold.
- Publish the binary-size table per feature subset.

Exit: a documented, measured answer to "what does an app pay for parts it does
not use".

### Phase 5 — UI toolkit features (PLANNED)

- `ui::Theme` (dark/light, token overrides, density).
- `ui::TypeScale` and text roles (caption/secondary/body/title/header), with
  wrapping/ellipsis policy.
- `ui::Context` carrying `Canvas`, `FrameInput`, theme, metrics and per-frame
  state; no boilerplate in apps.
- `ui::Id` + state store for hot/active/focus/scroll; keyboard navigation.
- Widgets migrated one at a time, each with a CPU-only test.
- Optional: a small declarative screen description interpreted by `ui/` for
  hot reload and less boilerplate. Data, not generated C++.

Exit: a demo screen can be built without hand-wiring every widget's state, and
the same screen is testable without a window.

### Phase 6 — Generator decision gate (DEFERRED)

Only after Phase 4 shows that file splits + GC sections + module targets + LTO
still leave an unacceptable binary or build-time cost.

If a generator is built, its contract is:

- It reads an app manifest (modules, widgets, features) and emits **build
  configuration and app-level screen data/code**, never a fork of framework
  source.
- The framework keeps one audited implementation. Generated code stays out of
  the framework's audit surface and has a source map/provenance file.
- No runtime interpreter in release unless the app opts in.

Rationale: a per-app source generator multiplies the code to audit, breaks
debug symbols and makes a framework bug fix not reach every app. It solves a
problem that link-time pruning usually solves better.

## 7. Unused code policy

What happens today:

- Source code always lives in the repository; it is not "in the program".
- A static archive (`vk_canvas_core`) is searched per object file (one per
  `.cc`). If the app never references a symbol from `plotview.o`, that object
  is not linked into the executable.
- But `renderer.o` is one 2,849-line object: using `Renderer` pulls all of it.
  `widgets.o` is one 730-line object: using one widget pulls all widgets.
- Without `-ffunction-sections -fdata-sections` + `--gc-sections` (or MSVC
  `/Gy /OPT:REF`), unused functions within a pulled object remain.
- Compile time and audit surface are paid regardless of linking.

Order of work:

1. Split files by subsystem/widget. Free at runtime; improves audit and tests.
2. Enable function/data sections and GC in Release; measure with `size` and
   `--print-gc-sections`.
3. Add module targets so a consumer links `base`/`draw`/`gpu`/`ui` explicitly.
4. Add feature flags only for genuinely optional, costly subsystems.
5. Add LTO as an option; measure.
6. Only then consider a manifest-driven generator, and only for build
   configuration/app screens.

The QML comparison is useful as an authoring goal, not an implementation
strategy. QML ships an engine and interprets a document; it does not generate a
C++ program per app. If a declarative layer is wanted, model it as data
interpreted by `ui/`, with hot reload in development and a compiled-in data
table in release.

## 8. Testing policy

Keep the family convention: plain `assert()`, `#undef NDEBUG`, no test
framework. Add a harness, not a framework.

| Label | What | Runs where | Failure policy |
|---|---|---|---|
| `unit` | pure logic, CPU only, no GPU/swapchain (a pure format adapter may include Vulkan headers) | Linux/Windows CI | must pass |
| `gpu` | headless surface, real swapchain, pixel probes | local + optional CI (lavapipe) | skip (77) if unavailable |
| `bench` | frame/startup/allocation/size measurements | local/nightly | regression report, not a hard gate at first |
| `fuzz` | UTF-8/UTF-16, text buffer, HTML sidecar parser | clang + libFuzzer; `dev/fuzz/` | crash = bug |

Harness rules:

- `dev/CMakeLists.txt` is a dev superbuild, not a root CMakeLists.
- `ctest --output-on-failure` is the one command. `tools/audit.sh` wraps
  configure + build + test + dependency check + size report.
- Every pure module gets a test binary. Modules without a test need a written
  reason in `docs/MODULES.md`.
- `canvas_records_test` inspects emitted `std::vector<float>` directly: record
  counts, geometry, clipping, shape packing. CPU-only.
- A header self-containment test compiles every public header alone.
- A dependency script fails on forbidden edges (core/base -> Vulkan, ui -> gpu,
  core -> platform SDK).
- `static_assert(!std::is_copy_constructible_v<Renderer>)` style checks pin
  ownership rules.
- GPU probes use the existing `exit 77` convention and stay optional.
- Golden/pixel tests assert structural properties (background, known rect
  centres, ink coverage) rather than exact hashes across drivers. Use lavapipe
  for deterministic runs when available.
- Fuzz targets live under `dev/fuzz/`; sanitizer builds are a CMake option.

Process rule: a refactor commit may move code and change include paths, but it
must not change behaviour. Behaviour changes are separate commits with tests.

## 9. UI toolkit notes

The toolkit's value is removing boilerplate, not hiding the engine:

- A screen is a function `void draw(ui::Context&)`. Immediate mode stays.
- The context owns per-frame state keyed by `ui::Id`: hover, active, focus,
  scroll, text-edit state. Apps stop threading these through every call.
- Theme is data: colour roles, type roles, spacing, stroke, radius, density.
  `col::` becomes the default dark theme, not a global hardcoded palette.
- Type hierarchy is one scale with a legible floor, not per-screen percentages.
  Caption/secondary/body/title/header are roles, not magic numbers.
- Widgets remain stateless functions over the context; each in its own TU so
  unused widgets are not linked.
- No retained scene graph, no runtime parser, no Vulkan in `ui/`.

## 10. Documentation set (canonical)

In `Vk_Canvas_Lb_LAW/docs/`:

- `VISION.md` — why, quality goals, non-goals, principles.
- `ARCHITECTURE.md` — layers, dependency rules, frame/data flow, ownership.
- `MODULES.md` — module, files, LOC, deps, tests, status. Checked by a script.
- `TESTING.md` — tiers, commands, how to add a test, skip conventions.
- `PERFORMANCE.md` — baselines, budgets, how to measure.
- `RELIABILITY.md` — checked calls, ownership, recovery paths, thread contract.
- `ROADMAP.md` — this document.
- `THIRD_PARTY.md` — submodules, licenses, generated files, provenance.
- `decisions/NNNN-*.md` — ADRs for irreversible choices.

`CLAUDE.md` and `AGENTS.md` become short pointers plus agent-specific rules,
not duplicate architecture manuals. The root workspace `AGENTS.md` gets a
"project charter" pointer and the current phase.

## 11. Open decisions

1. **Reference hardware/CI:** which machine and GPU define the performance
   baseline; is lavapipe acceptable for GPU CI.
2. **`archive`:** kept in `wip/` until a consumer needs generic persistence.
   Revisit then.
3. **`vk_canvas_core` transition:** how long the umbrella keeps linking
   `vk_canvas_ui`. Default: one cycle, then consumers move to
   `vk_canvas_engine` + `vk_canvas_ui` explicitly.

## 12. Commit discipline

- One mechanical change per commit: docs, harness, tests, WIP move, types,
  ownership, baseline.
- Each commit builds and passes the tests that exist at that point.
- Commit `Vk_Canvas_Lb_LAW` before `App_shell` when a change spans both.
- Use `./git_wrapper`, never plain `git commit`/`git push`.
- Never delete files as part of this work. Move WIP files with `git mv` and
  document why.
