#include "widgets.hh"

#include "widgets_internal.hh"

#include <string>

namespace widgets {
using detail::labelPad;
using detail::rowTextSize;
using detail::vcenter;

void drawDropdownField(Canvas& c, const Rect& row,
                       std::string_view label, std::string_view value,
                       bool hovered, const TextFit& fit,
                       const DropdownStyle& style) {
  float s = rowTextSize(row);
  float fieldW = row.w * 0.52f;
  Rect f = {row.x + row.w - fieldW, row.y, fieldW, row.h};

  std::string labelS(label);
  float labelSize = applyTextFit(c, labelS, f.x - row.x - labelPad(row), s, fit);
  c.text(labelS, row.x, vcenter(row, labelSize), labelSize, style.labelText);

  c.rect(f.x, f.y, f.w, f.h, hovered ? style.fieldHoverBg : style.fieldBg, f.h * 0.22f);

  // The value's zone ends where the chevron's begins, so with a fitting
  // policy the two can never touch.
  float chevronZone = f.h * 1.0f;
  std::string valueS(value);
  float valueSize = applyTextFit(c, valueS, f.w - f.h * 0.4f - chevronZone, s, fit);
  c.text(valueS, f.x + f.h * 0.4f, vcenter(f, valueSize), valueSize, style.valueText);
  // Disclosure chevron: a real triangle (analytic vector primitive — one
  // 3-line contour in the winding pass), not a glyph.
  float cx = f.x + f.w - f.h * 0.5f;
  float cy = f.y + f.h * 0.5f;
  float aw = f.h * 0.13f;   // half-width
  float ah = f.h * 0.16f;   // total height
  c.triangle(cx - aw, cy - ah * 0.5f,
             cx + aw, cy - ah * 0.5f,
             cx,      cy + ah * 0.5f, style.chevron);
}

}  // namespace widgets
