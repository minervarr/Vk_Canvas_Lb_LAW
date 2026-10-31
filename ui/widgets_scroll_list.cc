#include "widgets.hh"

#include <algorithm>
#include <string>

namespace widgets {

float listContentHeight(int n, float rowH) { return n * rowH; }

std::vector<ListRow> drawScrollList(Canvas& c, const Rect& area,
                                    const std::vector<std::string>& items,
                                    int selected, float scrollPx, float rowH,
                                    int hoverIndex, const TextFit& fit,
                                    const ScrollListStyle& style) {
  std::vector<ListRow> visible;
  c.rect(area.x, area.y, area.w, area.h, style.background, c.pad());
  const auto savedClip = c.saveClip();
  c.setClip(area.x, area.y, area.w, area.h);
  float s = rowH * 0.42f;
  for (int i = 0; i < (int)items.size(); i++) {
    float ry = area.y + i * rowH - scrollPx;
    if (ry + rowH < area.y || ry > area.y + area.h) continue;  // off-screen
    Rect r = {area.x, ry, area.w, rowH};
    bool sel = (i == selected);
    Color textColor = style.rowText;

    // Measure the row's text first — the Pill fill can hug it (fitWidth).
    float textX = r.x + c.pad();
    std::string item = items[(size_t)i];
    float itemSize = applyTextFit(c, item, r.w - c.pad() * 2.0f, s, fit);
    float textW = c.textWidth(item, itemSize);

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
    c.text(item, textX, r.y + (rowH - itemSize) * 0.5f, itemSize, textColor);
    visible.push_back({r, i});
  }
  c.restoreClip(savedClip);
  return visible;
}

}  // namespace widgets
