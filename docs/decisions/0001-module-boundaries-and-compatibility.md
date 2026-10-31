# 0001 — Module directories, CMake targets and the compatibility umbrella

Status: accepted (2026-10-31)

## Context

The engine grew as one set of sources: pure geometry, drawing, the Vulkan
renderer, widgets and uncompiled prototypes all lived under `core/`, and
`renderer.cc` alone was 2,849 lines. Nothing enforced the direction of
dependencies: `core/layout` could include `canvas.hh`, UI code could include
Vulkan, and `core/` could include Android SDK headers. A change that violated
the architecture still compiled.

Two properties had to survive the cleanup:

- Existing consumers include headers by bare name (`#include "canvas.hh"`) and
  link `vk_canvas_core`. Source compatibility was a locked decision.
- A consumer that never uses widgets must not be forced to link the toolkit.

## Decision

Physical directories follow the dependency layers:

```
core/base     value types, logging, animation, text buffer, undo/redo
core/input    input seam, frame input, gestures
core/layout   layout and type-scale maths
core/draw     Canvas and text helpers
core/gpu      renderer, overlay, image layer, output target, platform seam
ui/           theme, widgets, context, pager, text area, plot view
wip/          unported prototypes; not compiled
```

CMake mirrors the directories with static libraries `vk_canvas_base`,
`vk_canvas_draw`, `vk_canvas_gpu` and `vk_canvas_ui`. Each target propagates
its include directory as `PUBLIC`, so include names stay stable while the file
location remains an implementation detail. `vk_canvas_engine` is the INTERFACE
target for base + draw + gpu; `vk_canvas_core` is the compatibility umbrella and
also links `vk_canvas_ui`.

`tools/check_deps.py` enforces the edges by path: base has no Vulkan/platform
dependency, draw has no GPU dependency, UI includes no Vulkan or `gpu/` header,
`core/` includes no WIP header, and every compiled `.cc` appears in its target's
source list.

Uncompiled prototypes are quarantined in `wip/`, with `wip/README.md` stating
status and porting requirements. `archive` stays there until a consumer needs
generic persistence and a test exists.

## Consequences

- A consumer can link `vk_canvas_engine` and avoid `vk_canvas_ui` entirely.
  `VCE_BUILD_UI=OFF` also removes the toolkit from the build: only base, draw and
  gpu targets are configured. The option defaults ON so existing builds are
  unchanged; `vk_canvas_core` then reduces to the engine, so a widget reference
  fails instead of silently pulling a disabled target.
- The umbrella keeps old builds working, but it links the toolkit; new code
  links the precise targets. The umbrella keeps the UI link until a consumer has
  migrated to `vk_canvas_engine` + `vk_canvas_ui`; no consumer is present in this
  checkout, so removal is deliberately not scheduled by date.
- Directory boundaries are enforced, not aspirational. Moving a file across an
  edge now fails the audit until the target and rule are updated.
- `core/` no longer contains code that is not built, so "the source tree" and
  "the audited surface" mean the same thing.

## Rejected alternatives

- **Flat `core/` with documentation-only boundaries.** This was the state that
  produced the violations; prose did not stop them.
- **Extract `ui/` to its own repository.** It has no independent consumers yet,
  and a second repository would double the release/versioning work.
- **Make every file its own CMake target.** Include propagation and target count
  would grow without a measured link-time win; the static-library granularity is
  already one object per `.cc`.
