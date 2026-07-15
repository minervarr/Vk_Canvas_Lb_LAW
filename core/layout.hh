#pragma once
#include <algorithm>

#include "canvas.hh"  // Rect

// Resolution-robust layout math, pure and platform-agnostic — no Vulkan, no
// Canvas draw calls. Pages compute their layout from the current surface size
// every frame with these helpers instead of hardcoding absolute pixels; the
// same technique validated by matrix_player's recalcLayout() and the
// windows_ui_demo pages, promoted into core so every consumer app stops
// reimplementing it.

// Proportional geometry scale: a value declared at `referenceHeight` scales
// linearly with the actual surface height, floored (never zero/negative) so
// controls shrink gracefully on a small window but keep growing on a large
// one. This is the geometry sibling of responsive_text.hh's
// ResponsiveTextScale (which is percentage-of-height with a legibility floor
// in px — a different formula for a different job; they coexist).
struct UiScale {
  float referenceHeight;
  float floorScale = 0.5f;

  float factor(float actualHeight) const {
    return std::max(actualHeight / referenceHeight, floorScale);
  }
  // factor() with a ceiling on top of the floor. Layouts whose content stacks
  // vertically use this: uncapped scaling grows the stack faster than the
  // window (padding/gaps scale too), pushing the last rows off-screen on a
  // maximized window.
  float cappedFactor(float actualHeight, float cap) const {
    return std::min(factor(actualHeight), cap);
  }
  float scale(float value, float actualHeight) const {
    return value * factor(actualHeight);
  }
};

// Clamps a scroll offset so the view never scrolls above the content top
// (< 0) or past its bottom; content shorter than the view can't scroll.
inline float clampScroll(float scrollPx, float contentH, float viewH) {
  return std::max(0.0f, std::min(scrollPx, std::max(0.0f, contentH - viewH)));
}

// Dock helpers: mutate `container` in place (shrinking it) and return the
// strip cut off its edge. Chain them to carve a screen into nav/content/etc.
Rect dockTop(Rect& container, float thickness);
Rect dockBottom(Rect& container, float thickness);
Rect dockLeft(Rect& container, float thickness);
Rect dockRight(Rect& container, float thickness);

Rect centerIn(const Rect& container, float w, float h);

// Gap-chaining cursors: each next(w, h) returns the next rect in the row /
// column and advances by the size plus a fixed gap.
class RowCursor {
 public:
  RowCursor(float startX, float y, float gap) : x_(startX), y_(y), gap_(gap) {}
  Rect next(float w, float h) {
    Rect r{x_, y_, w, h};
    x_ += w + gap_;
    return r;
  }
 private:
  float x_, y_, gap_;
};

class ColumnCursor {
 public:
  ColumnCursor(float x, float startY, float gap) : x_(x), y_(startY), gap_(gap) {}
  Rect next(float w, float h) {
    Rect r{x_, y_, w, h};
    y_ += h + gap_;
    return r;
  }
 private:
  float x_, y_, gap_;
};
