# Vision

Status: canonical, 2026-10-31. The phased plan and current position live in
[`ROADMAP.md`](ROADMAP.md).

## Mission

Build a framework that lets an application be written once and run as a real
program on Windows, Linux/Wayland and Android — and make that framework small
enough, clear enough and tested enough that a new engineer or AI can audit it
from zero.

Three layers, one direction of dependency:

- **`core/`** — how it looks: Vulkan device/swapchain, canvas, text, images,
  shapes, input primitives, geometry and small data structures.
- **`ui/`** — how a screen is composed: theme tokens, type hierarchy, layout
  helpers, widgets, focus and per-frame UI state. Not required to use the
  engine; never includes Vulkan or a platform SDK.
- **`app_shell`** — how it runs: window, event loop, platform hosts, paths,
  orientation, crash handling and lifecycle.

The framework is not the product. Its consumers are apps (Matrix Player, UI
Demo, VideoPlayer and the next one). API decisions favour the next consumer,
not the current demo or the first app.

## Quality goals

1. **Auditable from zero.** Every feature can be traced from `docs/` to a
   module, its public contract, its test and its owner without tribal knowledge.
2. **Test-driven.** Pure logic has CPU-only tests; GPU paths have headless
   probes; every bug fix starts with a failing test. Refactors are
   behaviour-preserving and proven by tests, not inspection.
3. **Remarkable performance, measured.** No performance claim without a
   baseline. Steady-state UI must not allocate per frame. The fast paths stay:
   SDF shapes for primitives, MSDF for text, a lazy compute rasterizer, two
   frames in flight where the path allows it.
4. **Stability and reliability by construction.** Owned Vulkan objects have
   explicit ownership, checked results, validation-clean teardown, and tested
   recovery paths (resize, minimize, surface loss, device loss, HDR downgrade).
5. **Pay for what you use.** Unused subsystems must not tax an app's binary,
   startup, memory or build time. This is an engineering property, not a code
   generator trick.
6. **One canonical place per fact.** Code comments state invariants and
   contracts; rationale lives in `docs/decisions/`. Agent files and docs must
   not contradict each other.

## Principles

- Keep `core/` portable. Platform needs go through `core/gpu/platform.hh`; the two
  guarded exceptions (`log.hh`, `renderer.hh`'s Android camera half) are
  documented and scheduled for removal in Phase 1.
- Prefer a small explicit interface over a clever abstraction. A boundary that
  cannot be tested is not a boundary.
- One file, one subsystem. The linker's object-file granularity should match
  the auditor's mental model.
- Keep the fast path fast and the fallback obvious. Never silently truncate
  geometry; grow or fail loudly.
- A failure must be observable: checked Vulkan results, validation layers in
  Debug, recovery paths that report rather than crash.
- Move unported code to `wip/`; never leave it in a compiled directory. Do not
  delete history — move it and document what porting would require.

## Non-goals

- A QML-style runtime UI language.
- A source generator that forks or rewrites framework internals per app.
- A wholesale public-API rewrite for style before the boundaries and tests are
  in place.
- Retaining every historical behaviour that blocks a clean boundary. A
  compatibility shim may last one release, then is removed deliberately.
