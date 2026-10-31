#include "widgets.hh"

#include "widgets_internal.hh"

namespace widgets {
using detail::vcenter;

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
                   const std::string_view* options, int count, int selected,
                   const SegmentedStyle& style) {
  float s = row.h * 0.36f;
  for (int i = 0; i < count; i++) {
    bool sel = i == selected;
    Rect r = segmentRectAt(row, count, i);
    c.rect(r.x, r.y, r.w, r.h, sel ? style.selectedBg : style.unselectedBg, r.h * 0.28f);
    c.textCentered(options[i], r.x + r.w * 0.5f, vcenter(r, s), s,
                   sel ? style.selectedText : style.unselectedText);
  }
}

}  // namespace widgets
