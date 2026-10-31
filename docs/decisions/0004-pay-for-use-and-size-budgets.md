# 0004 — Pay-for-use: per-subsystem TUs, section GC and size budgets

Status: accepted (2026-10-31); generator gate deferred

## Context

A static archive is searched per object file. With one 2,849-line `renderer.cc`
and one 730-line `widgets.cc`, using one symbol pulled the whole subsystem. A
consumer that drew a rectangle paid for text, images, the renderer and every
widget.

The requirement is not "make the source tree small"; it is "an app pays at
runtime, startup, memory and build time only for what it uses". Compile time and
audit surface are paid regardless of linking, so file granularity matters too.

## Decision

1. **One file per subsystem or widget.** The renderer split is ADR 0002; the
   toolkit has one TU per widget (`ui/widgets_toggle.cc`, `ctx_toggle.cc`, …)
   plus shared helpers.
2. **Module targets** (`vk_canvas_base`, `vk_canvas_draw`, `vk_canvas_gpu`,
   `vk_canvas_ui`, and the `vk_canvas_engine` interface) so a consumer links
   only the layers it calls. ADR 0001.
3. **Function/data sections + section GC in Release** (`cmake/VceModules.cmake`):
   `-ffunction-sections -fdata-sections`, `-Wl,--gc-sections`; MSVC `/Gy` +
   `/OPT:REF`; Apple `-Wl,-dead_strip`. Debug builds stay section-free.
4. **Measured budgets per feature subset**, not a single binary-size claim:
   `dev/size/` link-only probes + `tools/size_budgets.txt`, run by
   `tools/size_report.sh` and `tools/audit.sh`. Budgets are local-machine gates
   with recorded provenance; `VK_SIZE_BUDGET=0` reports without gating.
5. **LTO is opt-in.** CMake IPO is available as `VCE_CANVAS_LTO` / `VCE_DEV_LTO`
   but is not a default.
6. **No module switches without a measurement.** Section GC already removes
   unused primitives, and the remaining archives are subsystem-sized; an
   `-DVCE_WITH_X` matrix would add configuration cost without a measured win.
7. **No per-app source generator.** See the gate below.

## Measured evidence

On the reference machine (Intel i7-11800H, Intel TGL UHD, GCC 16.2.1):

| Probe | Result |
|---|---|
| Primitive-only Canvas app, stripped, GC on | ~23 KB (was ~953 KB before the split + GC) |
| Layout-only app, stripped | ~14.5 KB |
| One widget (`size_ui_toggle`), stripped | ~1.04 MB |
| All widgets (`size_ui_all_widgets`), stripped | ~1.08 MB |

The widget probes are dominated by the font engine, which `vk_canvas_draw`
links; the toolkit itself adds only ~33 KB across every widget. That is why the
per-widget TU split matters: a toggle pulls one widget object, not a monolith.

LTO measurement: Release build +4.3%, binaries −0.4…−0.8%, `gpu_smoke_test` CPU
unchanged within noise. The tiny smoke scene cannot show a frame-time win, so
LTO stays opt-in until a real scene is measured.

## Generator decision gate

The roadmap's Phase 6 generator is **not triggered**. Phase 4 measurements plus
Phase 5 additions did not produce an unacceptable binary, build-time or audit
cost for the current consumers.

Re-open only if a consumer reports one of those costs with numbers. If built, a
generator may emit build configuration and app-level screen data/code; it must
never fork framework source. Generated code stays outside the framework's audit
surface and carries a provenance/source map. No runtime interpreter in release
unless the app opts in.

## Consequences

- The link-level floor is documented per feature subset, and a regression fails
  the audit on the machine that recorded it.
- Budgets are not a cross-machine performance claim; a compiler, libc or font
  submodule change requires a deliberate re-baseline.
- Some object files remain larger than one function; section GC handles unused
  functions inside them. Splitting further is justified by audit or compile time,
  not by a hypothetical link win.

## Rejected alternatives

- **A per-app source generator as the primary pruning mechanism.** It multiplies
  the code to audit, breaks debug symbols, and prevents a framework fix from
  reaching every app. Link-time pruning solves the same problem with one audited
  implementation.
- **Hard `#ifdef` feature matrix.** Configuration combinations without measured
  savings are a maintenance and test burden.
- **LTO by default.** It costs build time and showed no frame-time win.
