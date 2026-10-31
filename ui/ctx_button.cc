#include "widgets_context.hh"

#include "context_internal.hh"

namespace ui {

bool button(Context& c, Id id, const Rect& rect, std::string_view label) {
    c.registerFocusable(id);
    c.hovered(id, rect);
    bool activated = c.clicked(id, rect) || c.activate(id);

    Color bg = c.theme().colors.accent;
    if (c.hot(id) && !c.active(id)) {
        // Hover is a quiet lift, not a second palette entry: same accent, lighter.
        bg = {bg.r + (1.0f - bg.r) * 0.18f, bg.g + (1.0f - bg.g) * 0.18f,
              bg.b + (1.0f - bg.b) * 0.18f, bg.a};
    }
    if (c.active(id)) {
        bg = {bg.r * 0.82f, bg.g * 0.82f, bg.b * 0.82f, bg.a};
    }

    widgets::drawFitButton(c.canvas(), rect, label, bg, c.theme().colors.onAccent,
                           c.theme().radius, detail::bodyFit(c.type()));
    if (c.focused(id)) c.drawFocusRing(rect, c.theme().radius);
    return activated;
}

}  // namespace ui
