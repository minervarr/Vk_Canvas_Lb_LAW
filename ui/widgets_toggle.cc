#include "widgets.hh"

#include "widgets_internal.hh"

namespace widgets {
using detail::drawLabelFit;

Rect toggleSwitchRect(const Rect& row) {
  float h = row.h * 0.66f;
  float w = h * 1.8f;
  return {row.x + row.w - w, row.y + (row.h - h) * 0.5f, w, h};
}

void drawToggle(Canvas& c, const Rect& row, bool on, std::string_view label,
                const ToggleStyle& style) {
  Rect sw = toggleSwitchRect(row);
  drawLabelFit(c, label, row, sw.x);

  c.rect(sw.x, sw.y, sw.w, sw.h, on ? style.onColor : style.offColor, sw.h * 0.5f);
  float knob = sw.h * 0.82f;
  float ky = sw.y + (sw.h - knob) * 0.5f;
  float kx = on ? (sw.x + sw.w - knob - (sw.h - knob) * 0.5f)
                : (sw.x + (sw.h - knob) * 0.5f);
  c.rect(kx, ky, knob, knob, style.knobColor, knob * 0.5f);
}

}  // namespace widgets
