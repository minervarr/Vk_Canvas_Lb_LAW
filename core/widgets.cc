#include "widgets.hh"

#include <algorithm>

namespace widgets {

namespace {
// Vertically-centred text top for size `s` within a row of height `h`.
inline float vcenter(const Rect& r, float s) { return r.y + (r.h - s) * 0.5f; }
inline float rowTextSize(const Rect& r) { return r.h * 0.40f; }
inline float labelPad(const Rect& r) { return r.h * 0.30f; }

// Draw a left-aligned label that shrinks to fit within [row.x, limitX] so it
// never runs into the control on the right. Floors at 60% of the row text size.
void drawLabelFit(Canvas& c, std::string_view label, const Rect& row, float limitX) {
  float maxW = limitX - row.x - labelPad(row);
  float s = rowTextSize(row);
  float floor = s * 0.6f;
  while (s > floor && c.textWidth(label, s) > maxW) s -= row.h * 0.03f;
  c.text(label, row.x, vcenter(row, s), s, col::text);
}
}  // namespace

// ── Toggle ───────────────────────────────────────────────────────────────────
Rect toggleSwitchRect(const Rect& row) {
  float h = row.h * 0.66f;
  float w = h * 1.8f;
  return {row.x + row.w - w, row.y + (row.h - h) * 0.5f, w, h};
}

void drawToggle(Canvas& c, const Rect& row, bool on, std::string_view label) {
  Rect sw = toggleSwitchRect(row);
  drawLabelFit(c, label, row, sw.x);

  c.rect(sw.x, sw.y, sw.w, sw.h, on ? col::accent : col::track, sw.h * 0.5f);
  float knob = sw.h * 0.82f;
  float ky = sw.y + (sw.h - knob) * 0.5f;
  float kx = on ? (sw.x + sw.w - knob - (sw.h - knob) * 0.5f)
                : (sw.x + (sw.h - knob) * 0.5f);
  c.rect(kx, ky, knob, knob, col::thumb, knob * 0.5f);
}

// ── Stepper ──────────────────────────────────────────────────────────────────
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
                 std::string_view valueText) {
  float s = rowTextSize(row);
  StepperGeom g = stepperGeom(row);
  drawLabelFit(c, label, row, g.minus.x);
  c.button(g.minus.x, g.minus.y, g.minus.w, g.minus.h, "-", col::btnIdle, col::text, g.minus.h * 0.3f);
  c.button(g.plus.x,  g.plus.y,  g.plus.w,  g.plus.h,  "+", col::btnIdle, col::text, g.plus.h * 0.3f);
  c.rect(g.value.x, g.value.y, g.value.w, g.value.h, col::track, g.value.h * 0.2f);
  c.textCentered(valueText, g.value.x + g.value.w * 0.5f, vcenter(g.value, s), s, col::text);
}

// ── Slider ───────────────────────────────────────────────────────────────────
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
                std::string_view label, std::string_view valueText) {
  float s = rowTextSize(row);
  SliderGeom g = sliderGeom(row, t01);
  drawLabelFit(c, label, row, g.bar.x);
  c.rect(g.bar.x, g.bar.y, g.bar.w, g.bar.h, col::track, g.bar.h * 0.5f);
  // Filled portion up to the thumb.
  float fillW = g.thumb.x + g.thumb.w * 0.5f - g.bar.x;
  if (fillW > 0.0f) {
    // Drawn as a fast MSDF quad so drag doesn't trigger expensive curve compute
    c.quadMsdfRect(g.bar.x, g.bar.y + g.bar.h * 0.1f, fillW, g.bar.h * 0.8f, col::accent);
  }
  c.quadMsdfRect(g.thumb.x, g.thumb.y, g.thumb.w, g.thumb.h, col::thumb);
  c.textRight(valueText, row.x + row.w, vcenter(row, s), s, col::dim);
}

// ── Segmented ─────────────────────────────────────────────────────────────────
Rect segmentRectAt(const Rect& row, int count, int i) {
  if (count <= 0) return row;
  float gap = row.h * 0.12f;
  float segW = (row.w - gap * (count - 1)) / count;
  return {row.x + i * (segW + gap), row.y, segW, row.h};
}

std::vector<Rect> segmentRects(const Rect& row, int count) {
  std::vector<Rect> out;
  out.reserve(count > 0 ? count : 0);
  for (int i = 0; i < count; i++) out.push_back(segmentRectAt(row, count, i));
  return out;
}

void drawSegmented(Canvas& c, const Rect& row,
                   const std::string_view* options, int count, int selected) {
  float s = row.h * 0.36f;
  for (int i = 0; i < count; i++) {
    bool sel = i == selected;
    Rect r = segmentRectAt(row, count, i);
    c.rect(r.x, r.y, r.w, r.h, sel ? col::accent : col::btnIdle, r.h * 0.28f);
    c.textCentered(options[i], r.x + r.w * 0.5f, vcenter(r, s), s,
                   sel ? col::text : col::dim);
  }
}

// ── Dropdown field ────────────────────────────────────────────────────────────
void drawDropdownField(Canvas& c, const Rect& row,
                       std::string_view label, std::string_view value) {
  float s = rowTextSize(row);
  c.text(label, row.x, vcenter(row, s), s, col::text);
  float fieldW = row.w * 0.52f;
  Rect f = {row.x + row.w - fieldW, row.y, fieldW, row.h};
  c.rect(f.x, f.y, f.w, f.h, col::track, f.h * 0.22f);
  c.text(value, f.x + f.h * 0.4f, vcenter(f, s), s, col::text);
  c.textRight("v", f.x + f.w - f.h * 0.4f, vcenter(f, s), s, col::dim);
}

// ── ScrollList ────────────────────────────────────────────────────────────────
float listContentHeight(int n, float rowH) { return n * rowH; }

std::vector<ListRow> drawScrollList(Canvas& c, const Rect& area,
                                    const std::vector<std::string>& items,
                                    int selected, float scrollPx, float rowH) {
  std::vector<ListRow> visible;
  c.rect(area.x, area.y, area.w, area.h, col::panel2, c.pad());
  c.setClip(area.x, area.y, area.w, area.h);
  float s = rowH * 0.42f;
  for (int i = 0; i < (int)items.size(); i++) {
    float ry = area.y + i * rowH - scrollPx;
    if (ry + rowH < area.y || ry > area.y + area.h) continue;  // off-screen
    Rect r = {area.x, ry, area.w, rowH};
    if (i == selected)
      c.rect(r.x + c.pad() * 0.3f, r.y + rowH * 0.08f,
             r.w - c.pad() * 0.6f, rowH * 0.84f, col::accent, rowH * 0.18f);
    c.text(items[(size_t)i], r.x + c.pad(), r.y + (rowH - s) * 0.5f, s, col::text);
    visible.push_back({r, i});
  }
  c.clearClip();
  return visible;
}

// ── Group header ──────────────────────────────────────────────────────────────
void drawGroupHeader(Canvas& c, const Rect& row, std::string_view title) {
  float s = row.h * 0.5f;
  c.text(title, row.x, row.y + (row.h - s) * 0.5f, s, col::accent);
}

}  // namespace widgets
