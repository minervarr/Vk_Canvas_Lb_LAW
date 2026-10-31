#pragma once
// Shared drawing helpers for the per-widget translation units. Not part of the
// toolkit's public surface: widgets.hh is the contract.
#include <string>
#include <string_view>

#include "canvas.hh"
#include "widgets.hh"   // TextFit

namespace widgets {

// Result of applying a TextFit without forcing the caller to materialise a
// std::string. `text` points into the input unless truncation was required,
// in which case it points into `scratch` (which the caller owns and reuses).
// `width` is the measured width at `size`, so a centred caller does not walk
// the string a second time.
struct FittedText {
  std::string_view text;
  float size = 0.0f;
  float width = 0.0f;
};

// `knownWidth` lets a caller that has already measured `s` at `size` avoid a
// duplicate walk (-1 = measure it here).
FittedText fitText(Canvas& c, std::string_view s, std::string& scratch,
                   float maxW, float size, const TextFit& fit,
                   float knownWidth = -1.0f);

namespace detail {

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
  float w = c.textWidth(label, s);
  // Text width is linear in size: one division gives the exact fit. The old
  // loop stepped down by row.h*0.03 and measured the whole label at every
  // step (up to ~13 full walks per row, every frame).
  if (w > maxW && w > 0.0f) s = std::max(s * 0.6f, s * (maxW / w));
  c.text(label, row.x, vcenter(row, s), s, col::text);
}

}  // namespace detail
}  // namespace widgets
