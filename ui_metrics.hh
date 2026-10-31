#pragma once
#include "type_scale.hh"   // vk_canvas base: UiMetrics, computeUiMetrics()

// The type scale now lives in the engine's base layer
// (core/layout/type_scale.hh). It used to be here and depended on a
// build-generated `ui_min_text_size.gen.h`; the app now passes its font-derived
// legibility floor to computeUiMetrics(shortSide, minReadablePx). App_shell
// therefore builds without any generated header, and this file keeps only the
// app-specific grid geometry below.

// The album grid's TOP pad, derived from its horizontal pad and the cell's
// own centering slack — never authored independently.
//
// It lives here because it is app layout, not theme: resolution-robust geometry
// derived from one place rather than hand-tuned per screen. Pure and
// header-only so ui_metrics_test can assert it without linking a Canvas.
//
// Clamped at 0 slack: a cell narrower than its art is degenerate, and a
// negative pad would push the first row off the top of the page.
constexpr int gridTopPad(int padXpx, int cellStepX, int artSize) {
    const int slack = cellStepX - artSize;
    return padXpx + (slack > 0 ? slack / 2 : 0);
}

// ── Keeping a resize pointed at the same records ────────────────────────────
//
// A grid scroll offset is in PIXELS, but the thing a listener is looking at is
// a set of TILES. The moment the column count changes those stop agreeing: the
// same pixel offset is a different row, so resizing the window used to
// teleport them somewhere else in the library entirely.
//
// So the offset is converted to a tile index before the layout changes and
// back afterwards. Pure integer math, here rather than in player_view.cc, so
// ui_metrics_test can assert it without a window (same reason gridTopPad()
// lives here).
//
// The caller must still clampScroll() the result: preserving the anchor says
// nothing about whether it still FITS, and a wider window makes the grid
// shorter. An offset past the end draws no tiles at all.

// Which tile sits at the top-left, given the layout that produced `scrollY`.
constexpr int gridAnchorTile(int scrollY, int tileStepY, int cols) {
    if (tileStepY <= 0 || cols <= 0 || scrollY <= 0) return 0;
    return (scrollY / tileStepY) * cols;
}

// The scroll offset that puts `anchorTile` back at the top-left under a new
// layout. Truncating toward the row START is deliberate: it scrolls back a
// fraction of a row rather than forward, so the anchor is always fully
// visible instead of clipped at the top edge.
constexpr int gridScrollForAnchor(int anchorTile, int tileStepY, int cols) {
    if (tileStepY <= 0 || cols <= 0 || anchorTile <= 0) return 0;
    return (anchorTile / cols) * tileStepY;
}
