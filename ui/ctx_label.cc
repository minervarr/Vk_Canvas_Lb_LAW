#include "widgets_context.hh"

#include <string>

#include "context_internal.hh"

namespace ui {

void label(Context& c, const Rect& row, std::string_view text,
           TypeRole role, bool centered) {
    TypeStyle style = typeStyle(c.type(), role);
    std::string fitted(text);
    float size = widgets::applyTextFit(c.canvas(), fitted, row.w, style.size, style.fit);
    float y = row.y + (row.h - size) * 0.5f;
    Color color = typeColor(c.theme(), role);
    if (centered) {
        c.canvas().textCentered(fitted, row.x + row.w * 0.5f, y, size, color);
    } else {
        c.canvas().text(fitted, row.x, y, size, color);
    }
}

void groupHeader(Context& c, const Rect& row, std::string_view title) {
    label(c, row, title, TypeRole::Header);
}

}  // namespace ui
