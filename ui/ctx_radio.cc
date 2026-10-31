#include "widgets_context.hh"

#include "context_internal.hh"

namespace ui {

bool radio(Context& c, Id id, const Rect& row, std::string_view label,
           int& selected, int value) {
    c.registerFocusable(id);
    c.hovered(id, row);
    bool changed = c.clicked(id, row) || c.activate(id);
    if (changed && selected != value) {
        selected = value;
    } else {
        changed = false;
    }

    Rect hit = widgets::drawRadioRow(c.canvas(), row, selected == value, c.hot(id),
                                     label, detail::bodyFit(c.type()),
                                     detail::radioStyle(c.theme()));
    if (c.focused(id)) c.drawFocusRing(hit, c.theme().radius);
    return changed;
}

}  // namespace ui
