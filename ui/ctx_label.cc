#include "widgets_context.hh"

#include <string>

#include "context_internal.hh"
#include "text_font.hh"       // FontStyle
#include "widgets_internal.hh"

namespace ui {

void label(Context& c, const Rect& row, std::string_view text,
           TypeRole role, bool centered) {
    TypeStyle style = typeStyle(c.type(), role);
    std::string scratch;
    widgets::FittedText fitted{text, style.size, 0.0f};

    // kTextFree is the common Body case: no measure and no string copy unless
    // the caller asked for centring. Roles with a fit policy measure once;
    // only the overflow case touches `scratch`.
    if (style.fit.shrink || style.fit.ellipsis)
        fitted = widgets::fitText(c.canvas(), text, scratch, row.w,
                                  style.size, style.fit);
    else if (centered)
        fitted.width = c.canvas().textWidthStyled(text, style.size, FontStyle::Roman);

    float y = row.y + (row.h - fitted.size) * 0.5f;
    Color color = typeColor(c.theme(), role);
    if (centered) {
        c.canvas().text(fitted.text,
                        row.x + (row.w - fitted.width) * 0.5f,
                        y, fitted.size, color);
    } else {
        c.canvas().text(fitted.text, row.x, y, fitted.size, color);
    }
}

void groupHeader(Context& c, const Rect& row, std::string_view title) {
    label(c, row, title, TypeRole::Header);
}

}  // namespace ui
