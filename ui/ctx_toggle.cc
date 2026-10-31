#include "widgets_context.hh"

#include "context_internal.hh"

namespace ui {

bool toggle(Context& c, Id id, const Rect& row, std::string_view label, bool& value) {
    c.registerFocusable(id);
    c.hovered(id, row);
    bool changed = c.clicked(id, row) || c.activate(id);
    if (changed) value = !value;

    widgets::drawToggle(c.canvas(), row, value, label, detail::toggleStyle(c.theme()));

    if (c.focused(id)) {
        Rect sw = widgets::toggleSwitchRect(row);
        c.drawFocusRing(sw, sw.h * 0.5f);
    }
    return changed;
}

}  // namespace ui
