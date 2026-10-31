#include "widgets_context.hh"

#include <algorithm>
#include <cmath>

#include "context_internal.hh"
#include "keys.hh"

namespace ui {

namespace {
int rowAt(const Rect& area, const FrameInput& input, float scrollPx, float rowHeight,
          int count) {
    if (rowHeight <= 0.0f) return -1;
    if (!area.contains(input.pointerX, input.pointerY)) return -1;
    const float localY = input.pointerY - area.y + scrollPx;
    const int index = static_cast<int>(std::floor(localY / rowHeight));
    return (index >= 0 && index < count) ? index : -1;
}
}  // namespace

int scrollList(Context& c, Id id, const Rect& area,
               const std::vector<std::string>& items, int& selected,
               float rowHeight, const widgets::TextFit& fit) {
    c.registerFocusable(id);
    c.hovered(id, area);

    WidgetState& state = c.state(id);
    const int count = static_cast<int>(items.size());
    const float contentH = widgets::listContentHeight(count, rowHeight);
    const float maxScroll = std::max(0.0f, contentH - area.h);
    state.scrollPx = std::clamp(state.scrollPx, 0.0f, maxScroll);

    // Wheel: positive delta scrolls up, matching the platform wheel convention.
    if (area.contains(c.input().pointerX, c.input().pointerY) &&
        c.input().wheelDelta != 0.0f) {
        state.scrollPx = std::clamp(state.scrollPx - c.input().wheelDelta * rowHeight,
                                    0.0f, maxScroll);
    }

    if (c.focused(id) && count > 0) {
        bool moved = false;
        if (c.input().keyWentDown(key::Down)) {
            selected = std::min(count - 1, selected + 1);
            moved = true;
        }
        if (c.input().keyWentDown(key::Up)) {
            selected = std::max(0, selected - 1);
            moved = true;
        }
        if (c.input().keyWentDown(key::Home)) {
            selected = 0;
            moved = true;
        }
        if (c.input().keyWentDown(key::End)) {
            selected = count - 1;
            moved = true;
        }
        // Keep the selected row visible after keyboard movement. Do not do
        // this on every frame: it would undo wheel scrolling.
        if (moved) {
            const float top = static_cast<float>(selected) * rowHeight;
            const float bottom = top + rowHeight;
            if (top < state.scrollPx) state.scrollPx = top;
            if (bottom > state.scrollPx + area.h) state.scrollPx = bottom - area.h;
            state.scrollPx = std::clamp(state.scrollPx, 0.0f, maxScroll);
        }
    }

    c.press(id, area);
    const int hover = rowAt(area, c.input(), state.scrollPx, rowHeight, count);

    int clicked = -1;
    if (c.input().pointerWentUp && c.active(id)) {
        clicked = rowAt(area, c.input(), state.scrollPx, rowHeight, count);
        if (clicked >= 0) selected = clicked;
    }

    widgets::drawScrollList(c.canvas(), area, items, selected, state.scrollPx,
                            rowHeight, hover, fit, detail::scrollListStyle(c.theme()));
    if (c.focused(id)) c.drawFocusRing(area, c.theme().radius);
    return clicked;
}

}  // namespace ui
