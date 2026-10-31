# 0005 — Immediate-mode `ui::Context` with toolkit-owned state

Status: accepted (2026-10-31)

## Context

The widget layer was a set of functions whose callers threaded hover, active,
focus, scroll and text-edit state through every signature. A screen therefore
repeated the same state-holding boilerplate, and the same screen could not be
driven without a window. The theme was a hardcoded `col::` palette, and the type
scale lived in `App_shell` behind a build-generated header, so the shell could
not build standalone.

Two directions were possible: a retained widget tree (Qt/QML-like) or an
immediate-mode toolkit over the existing `Canvas`/`FrameInput` model.

## Decision

A screen is a function `void draw(ui::Context&)`. `ui::Context` binds one
frame's `Canvas`, `FrameInput`, a `Theme`, a `TypeScale` and a persistent
`StateStore`:

- `ui::Id` (stable FNV hash) keys per-widget hot/active/focus/scroll and
  per-field text-edit state.
- `StateStore::beginFrame()` clears transient hot flags and focus-order
  registrations; held state survives. `endFrame()` applies Tab/Shift+Tab focus
  movement, Escape blur and pointer-release cleanup.
- Widgets are stateless functions over the context (`ui::toggle(ctx, id, …)`).
  They do not own app values; the app keeps its bools, indices and models.
- Theme is data: colour roles, spacing, strokes, radius, density, with dark and
  light presets and copy-and-override tokens. `col::` remains the default dark
  palette, not a global hardcoded design.
- Type hierarchy is one scale with a legibility floor; caption/secondary/body/
  title/header are roles. The floor is an app argument, not a generated header
  (ADR 0009).
- `ui/` contains no Vulkan, no `gpu/` header and no `Renderer`; it draws through
  `Canvas` and reads `FrameInput`. The legacy `widgets::` API remains for source
  compatibility.

The toolkit ships a CPU-testable demo screen (`ui/demo/settings_screen.*`) that
uses only app values plus `ui::Context`; `context_demo_test` drives it headlessly.

## Consequences

- Apps stop hand-wiring interaction state, and a screen is testable without a
  window or a GPU.
- There is no retained scene graph, no retained widget allocation per frame and
  no runtime UI parser. The immediate-mode model and its allocation behaviour
  stay visible.
- Per-frame state is keyed by `Id`, so duplicate IDs in one screen alias state;
  IDs must be stable across frames. This is the main authoring rule.
- The declarative screen description mentioned as optional in Phase 5 is not
  implemented. If wanted, it must be a data layer interpreted by `ui/`, opt-in,
  with a compiled-in table for release — not a source generator (ADR 0004).
- Focus order is registration order within a frame; a screen that conditionally
  hides widgets changes the tab order unless it registers placeholders.

## Rejected alternatives

- **Retained widget tree.** It duplicates state ownership (app value vs widget
  node), needs a lifetime/allocation model, and makes the CPU-only test path
  heavier. The engine's frame model is already immediate.
- **QML-style runtime language.** A parser and binding engine would become a
  second implementation to audit, and the roadmap explicitly lists it as a
  non-goal for this phase.
- **Keep state in every widget signature.** That is the boilerplate this
  decision removes; it also makes headless testing awkward.
