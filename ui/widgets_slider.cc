#include "widgets.hh"

#include "widgets_internal.hh"

#include <algorithm>

namespace widgets {
using detail::drawLabelFit;
using detail::rowTextSize;
using detail::vcenter;

namespace {
// Bar horizontal extent: label on the left, value on the right.
void sliderBarX(const Rect& row, float& x0, float& x1) {
  float labelW = row.w * 0.34f;
  float valueW = row.w * 0.16f;
  x0 = row.x + labelW;
  x1 = row.x + row.w - valueW;
}
}  // namespace

SliderGeom sliderGeom(const Rect& row, float t01) {
  float x0, x1; sliderBarX(row, x0, x1);
  t01 = std::clamp(t01, 0.0f, 1.0f);
  float th = row.h * 0.22f;
  SliderGeom g;
  g.bar = {x0, row.y + (row.h - th) * 0.5f, x1 - x0, th};
  float knob = row.h * 0.55f;
  float kx = x0 + t01 * (x1 - x0) - knob * 0.5f;
  g.thumb = {kx, row.y + (row.h - knob) * 0.5f, knob, knob};
  return g;
}

float sliderValueAt(const Rect& row, float px) {
  float x0, x1; sliderBarX(row, x0, x1);
  if (x1 <= x0) return 0.0f;
  return std::clamp((px - x0) / (x1 - x0), 0.0f, 1.0f);
}

void drawSlider(Canvas& c, const Rect& row, float t01,
                std::string_view label, std::string_view valueText,
                const SliderStyle& style) {
  float s = rowTextSize(row);
  SliderGeom g = sliderGeom(row, t01);
  drawLabelFit(c, label, row, g.bar.x);
  c.rect(g.bar.x, g.bar.y, g.bar.w, g.bar.h, style.track, g.bar.h * 0.5f);
  // Filled portion up to the thumb.
  float fillW = g.thumb.x + g.thumb.w * 0.5f - g.bar.x;
  if (fillW > 0.0f) {
    // Drawn as a fast MSDF quad so drag doesn't trigger expensive curve compute
    c.quadMsdfRect(g.bar.x, g.bar.y + g.bar.h * 0.1f, fillW, g.bar.h * 0.8f, style.fill);
  }
  c.quadMsdfRect(g.thumb.x, g.thumb.y, g.thumb.w, g.thumb.h, style.thumb);
  c.textRight(valueText, row.x + row.w, vcenter(row, s), s, style.valueText);
}

}  // namespace widgets
