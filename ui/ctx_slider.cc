#include "widgets_context.hh"

#include <algorithm>
#include <cstdio>
#include <string>

#include "context_internal.hh"
#include "keys.hh"

namespace ui {

namespace {
void percentText(float value, char (&buffer)[16]) {
    std::snprintf(buffer, sizeof(buffer), "%.0f%%", value * 100.0f);
}
}  // namespace

bool slider(Context& c, Id id, const Rect& row, std::string_view label,
            float& value, std::string_view valueText) {
    c.registerFocusable(id);
    c.hovered(id, row);

    bool changed = false;
    if (c.press(id, row)) {
        value = widgets::sliderValueAt(row, c.input().pointerX);
        changed = true;
    } else if (c.active(id) && c.input().pointerDown) {
        float next = widgets::sliderValueAt(row, c.input().pointerX);
        if (next != value) {
            value = next;
            changed = true;
        }
    }

    // Keyboard adjustment while focused: arrows and PageUp/PageDown. The focus
    // ring alone is not enough for a keyboard-only screen.
    if (c.focused(id)) {
        const float step = 0.05f;
        const float page = 0.10f;
        if (c.input().keyWentDown(key::Left) || c.input().keyWentDown(key::Down)) {
            value = std::max(0.0f, value - step);
            changed = true;
        }
        if (c.input().keyWentDown(key::Right) || c.input().keyWentDown(key::Up)) {
            value = std::min(1.0f, value + step);
            changed = true;
        }
        if (c.input().keyWentDown(key::PageDown)) {
            value = std::max(0.0f, value - page);
            changed = true;
        }
        if (c.input().keyWentDown(key::PageUp)) {
            value = std::min(1.0f, value + page);
            changed = true;
        }
    }

    char buffer[16];
    std::string_view text = valueText;
    if (text.empty()) { percentText(value, buffer); text = buffer; }
    widgets::drawSlider(c.canvas(), row, value, label, text, detail::sliderStyle(c.theme()));
    if (c.focused(id)) c.drawFocusRing(row, c.theme().radius);
    return changed;
}

}  // namespace ui
