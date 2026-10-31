# 0009 — One type scale, one-pass fit and the solid-texel fallback

Status: accepted (2026-10-31)

## Context

Text sizing and text fitting had each accumulated a local fix:

- The type scale lived in `App_shell/ui_metrics.hh` and depended on a
  build-generated header, so the shell could not build without a consumer app
  and a real font. The generated constant was the smallest legible size.
- `splitTwoLines()` measured every prefix of a string separately to find the
  longest that fits: `textWidthStyled(s.substr(0, i))` per codepoint. That is a
  heap allocation and a full re-walk per codepoint — O(n²) work and O(n)
  allocations, per album tile, every frame the grid redrew.
- The MSDF solid-colour rect fallback asked the font to lay out `'I'` at a
  hardcoded 100 px. That was harmless for a distance field baked once per size,
  and a trap for a per-size raster cache, which requested a 100 px cell nothing
  would ever draw. It also ignored `q.draw`, so a font lacking the glyph sampled
  (0,0) — a transparent texel — and the "solid" rect was invisible.

## Decision

**Type scale.** There is one scale, anchored at the app's real render height and
the app's legibility floor. The smallest role is pinned to the floor; every other
role is `floor * kUiTypeRatio^n`. Because all roles derive from one clamped
value, the scale clamps uniformly below the reference height instead of the
smallest role clamping alone and flattening the hierarchy.

The floor is an argument (`TypeScale` / `typeScale(shortSide, minReadablePx)`),
not a generated header. `App_shell` depends on base modules only and the app owns
its font measurement; `AppShellMinTextSize.cmake` remains optional for apps that
want the measured constant.

The caller passes the window's **short side**, not its height. The two are the
same while every window is wider than tall; a 1080×1920 monitor would otherwise
scale 1.78× and a 1080×2400 phone 2.22×, inflating every bar because the screen
is tall, not because it is big.

**Fit.** `Canvas::prefixFitStyled()` accumulates the same advances in one pass
and returns the longest fitting prefix as a byte offset, plus the last space
before the cut. `splitTwoLines()` and the truncation helpers use it. The cut is
identical to the per-prefix loop because it accumulates the same advances in the
same order; the tests pin the equivalence. No per-prefix allocation and no
second string walk.

**Solid texel.** `Canvas::quadMsdfRect()` first asks the font for a reserved
solid texel (`solidTexel()`). If the font cannot reserve one, it lays out `'I'`
at the requested draw size and uses the middle of the resulting quad; if the
glyph is absent (`q.draw == false`), it falls back to a plain SDF rect. The
hardcoded 100 px request and the ignored `q.draw` are gone.

## Consequences

- A screen derives every text size from one factor and one floor; changing the
  floor keeps the hierarchy instead of clamping only the caption.
- Text fitting cost is linear in the string length and allocation-free after
  capacity is warm.
- A font without a reserved texel still gets a solid rect, at the size it will
  actually draw, and a missing `'I'` no longer produces an invisible rect.
- `type_scale` is in `core/layout` (base layer), so `App_shell` and `ui/` can
  both use it without a generated header.

## Rejected alternatives

- **Per-screen percentage sizes.** They drift, break the legibility floor and
  make hierarchy a matter of local taste.
- **Keep the generated floor constant.** It made a library depend on a consumer
  build step and blocked standalone builds.
- **Measure every prefix.** It is correct but quadratic and allocates per
  codepoint; the one-pass accumulator produces the same cut.
