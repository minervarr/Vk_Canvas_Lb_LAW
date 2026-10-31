#include "widgets_context.hh"

#include <string>

#include "context_internal.hh"

namespace ui {

bool textField(Context& c, Id id, const Rect& row, std::string& text,
               std::string_view placeholder) {
    c.registerFocusable(id);
    TextSlot& slot = c.textSlot(id);
    if (!slot.initialized) {
        slot.state.text = text;
        slot.state.cursorByte = text.size();
        slot.state.selectionAnchor = slot.state.cursorByte;
        slot.initialized = true;
    }

    bool pressed = c.press(id, row);

    bool changed = false;
    if (c.focused(id)) {
        if (pressed) {
            changed |= widgets::textFieldHandleClick(
                slot.state, c.canvas(), row, c.input(), c.nowSeconds(),
                detail::textFieldStyle(c.theme()));
        }
        changed |= widgets::textFieldHandleInput(slot.state, c.input(), nullptr);
    }

    widgets::drawTextField(c.canvas(), row, slot.state, c.focused(id), placeholder,
                           detail::textFieldStyle(c.theme()));
    if (c.focused(id)) c.drawFocusRing(row, c.theme().radius);

    text = slot.state.text;
    return changed;
}

}  // namespace ui
