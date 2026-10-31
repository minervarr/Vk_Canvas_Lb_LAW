#pragma once
// Shared drawing helpers for the per-widget translation units. Not part of the
// toolkit's public surface: widgets.hh is the contract.
#include <string_view>

#include "canvas.hh"

namespace widgets::detail {

// Vertically-centred text top for size `s` within a row of height `h`.
inline float vcenter(const Rect& r, float s) { return r.y + (r.h - s) * 0.5f; }
inline float rowTextSize(const Rect& r) { return r.h * 0.40f; }
inline float labelPad(const Rect& r) { return r.h * 0.30f; }

// Draw a left-aligned label that shrinks to fit within [row.x, limitX] so it
// never runs into the control on the right. Floors at 60% of the row text size.
inline void drawLabelFit(Canvas& c, std::string_view label, const Rect& row,
                         float limitX) {
  float maxW = limitX - row.x - labelPad(row);
  float s = rowTextSize(row);
  float floor = s * 0.6f;
  while (s > floor && c.textWidth(label, s) > maxW) s -= row.h * 0.03f;
  c.text(label, row.x, vcenter(row, s), s, col::text);
}

}  // namespace widgets::detail
