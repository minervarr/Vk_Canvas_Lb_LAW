#include "widgets.hh"

#include "widgets_internal.hh"

namespace widgets {
using detail::drawLabelFit;
using detail::rowTextSize;
using detail::vcenter;

StepperGeom stepperGeom(const Rect& row) {
  float bs = row.h * 0.92f;
  float vw = row.h * 2.1f;
  float gap = row.h * 0.12f;
  float clusterW = bs + gap + vw + gap + bs;
  float cx = row.x + row.w - clusterW;
  float y = row.y + (row.h - bs) * 0.5f;
  StepperGeom g;
  g.minus = {cx, y, bs, bs};
  g.value = {cx + bs + gap, y, vw, bs};
  g.plus  = {cx + bs + gap + vw + gap, y, bs, bs};
  return g;
}

void drawStepper(Canvas& c, const Rect& row, std::string_view label,
                 std::string_view valueText, const StepperStyle& style) {
  float s = rowTextSize(row);
  StepperGeom g = stepperGeom(row);
  drawLabelFit(c, label, row, g.minus.x);
  c.button(g.minus.x, g.minus.y, g.minus.w, g.minus.h, "-", style.buttonBg, style.buttonText, g.minus.h * 0.3f);
  c.button(g.plus.x,  g.plus.y,  g.plus.w,  g.plus.h,  "+", style.buttonBg, style.buttonText, g.plus.h * 0.3f);
  c.rect(g.value.x, g.value.y, g.value.w, g.value.h, style.valueBg, g.value.h * 0.2f);
  c.textCentered(valueText, g.value.x + g.value.w * 0.5f, vcenter(g.value, s), s, style.valueText);
}

}  // namespace widgets
