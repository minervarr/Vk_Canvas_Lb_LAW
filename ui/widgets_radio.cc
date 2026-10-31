#include "widgets.hh"

#include "widgets_internal.hh"

#include <algorithm>
#include <string>

namespace widgets {
using detail::rowTextSize;
using detail::vcenter;

Rect drawRadioRow(Canvas& c, const Rect& row, bool selected, bool hovered,
                  std::string_view label, const TextFit& fit, const RadioStyle& style) {
  float dotD = row.h * 0.40f;
  float pad  = row.h * 0.34f;   // anchor margin: left of the dot, right of the label
  float gap  = row.h * 0.32f;   // dot → label spacing
  Rect dot{row.x + pad, row.y + (row.h - dotD) * 0.5f, dotD, dotD};

  float labelX = dot.x + dot.w + gap;
  std::string text(label);
  float s = rowTextSize(row);
  float drawSize = applyTextFit(c, text, row.x + row.w - labelX - pad, s, fit);
  float textW = c.textWidth(text, drawSize);

  // Content-fitted hit/highlight rect: full row height (matches the row/button
  // height exactly — no vertical inset), only as wide as the dot + label + pad.
  Rect hit{row.x, row.y, (labelX + textW + pad) - row.x, row.h};
  {
    float rad = std::min(style.radius, hit.h * 0.5f);
    if (selected && style.selBg.a > 0.0f)
      c.rect(hit.x, hit.y, hit.w, hit.h, style.selBg, rad);
    else if (hovered)
      c.rect(hit.x, hit.y, hit.w, hit.h, style.hoverBg, rad);
    // Left accent bar marks the selected row (nav-style indicator).
    if (selected && style.selBar.a > 0.0f)
      c.rect(hit.x, hit.y, 3.0f, hit.h, style.selBar, rad);
  }

  // A rounded square with radius == half-size approximates a dot — Canvas has
  // no circle primitive (same technique as Pager::drawDots).
  c.rect(dot.x, dot.y, dot.w, dot.h, selected ? style.dotOn : style.dotOff, dot.w * 0.5f);
  c.text(text, labelX, vcenter(row, drawSize), drawSize,
         selected ? style.textOn : style.textOff);
  return hit;
}

}  // namespace widgets
