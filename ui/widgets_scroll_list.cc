#include "widgets.hh"

#include <algorithm>
#include <string>

#include "widgets_internal.hh"

namespace widgets {

float listContentHeight(int n, float rowH) { return n * rowH; }

void drawScrollListInto(Canvas& c, const Rect& area,
                        const std::vector<std::string>& items,
                        int selected, float scrollPx, float rowH,
                        int hoverIndex, const TextFit& fit,
                        const ScrollListStyle& style,
                        std::vector<ListRow>* visible) {
  c.rect(area.x, area.y, area.w, area.h, style.background, c.pad());
  const auto savedClip = c.saveClip();
  c.setClip(area.x, area.y, area.w, area.h);
  float s = rowH * 0.42f;
  std::string scratch;
  for (int i = 0; i < (int)items.size(); i++) {
    float ry = area.y + i * rowH - scrollPx;
    if (ry + rowH < area.y || ry > area.y + area.h) continue;  // off-screen
    Rect r = {area.x, ry, area.w, rowH};
    bool sel = (i == selected);
    Color textColor = style.rowText;

    // A string_view into the caller's item unless the fit policy truncates;
    // the old code copied every visible item every frame.
    float textX = r.x + c.pad();
    const FittedText item = fitText(c, items[(size_t)i], scratch,
                                    r.w - c.pad() * 2.0f, s, fit);
    float textW = item.width;

    if (style.selection == ListSelectionStyle::Pill) {
      // Full row height (matches the action-button height exactly).
      float px, pw;
      if (style.fitWidth) {                       // hug the text
        px = textX - c.pad() * 0.5f;
        pw = textW + c.pad();
      } else {                                    // full-width row
        px = r.x + c.pad() * 0.3f;
        pw = r.w - c.pad() * 0.6f;
      }
      float rad = std::min(style.radius, rowH * 0.5f);
      if (sel)
        c.rect(px, r.y, pw, rowH, style.pillColor, rad);
      else if (i == hoverIndex)
        c.rect(px, r.y, pw, rowH, style.hoverBg, rad);
      // Left accent bar marks the selected row (nav-style indicator).
      if (sel && style.selectedBar.a > 0.0f)
        c.rect(px, r.y, 3.0f, rowH, style.selectedBar, rad);
      if (sel) textColor = style.pillText;
    } else {  // BottomBorder — flat hover fill + underline on every row
      if (i == hoverIndex)
        c.rect(r.x, r.y, r.w, r.h, style.hoverBg);
      float borderThick = sel ? 2.0f : 1.0f;
      c.rect(r.x, r.y + r.h - borderThick, r.w, borderThick,
             sel ? style.borderSelected : style.borderUnselected);
      if (sel) textColor = style.borderSelected;
    }
    if (!sel && i == hoverIndex) textColor = style.hoverText;
    c.text(item.text, textX, r.y + (rowH - item.size) * 0.5f, item.size, textColor);
    if (visible) visible->push_back({r, i});
  }
  c.restoreClip(savedClip);
}

std::vector<ListRow> drawScrollList(Canvas& c, const Rect& area,
                                    const std::vector<std::string>& items,
                                    int selected, float scrollPx, float rowH,
                                    int hoverIndex, const TextFit& fit,
                                    const ScrollListStyle& style) {
  std::vector<ListRow> visible;
  drawScrollListInto(c, area, items, selected, scrollPx, rowH, hoverIndex,
                     fit, style, &visible);
  return visible;
}

}  // namespace widgets
