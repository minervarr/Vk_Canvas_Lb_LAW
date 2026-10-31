#include "context.hh"

#include "keys.hh"

namespace ui {

Context::Context(Canvas& canvas, FrameInput& input, StateStore& store,
                 Theme theme, TypeScale type, double nowSeconds)
    : canvas_(canvas), input_(input), store_(store), theme_(theme), type_(type),
      nowSeconds_(nowSeconds) {
    store_.beginFrame();
    // A press anywhere is a new gesture: the previous active widget must not
    // keep receiving the drag if this press lands elsewhere.
    if (input_.pointerWentDown) store_.setActive(0);
}

Context::~Context() {
    store_.endFrame(input_);
}

bool Context::hovered(Id id, const Rect& rect) {
    const bool inside = rect.contains(input_.pointerX, input_.pointerY);
    if (inside) store_.setHot(id);
    return inside;
}

bool Context::clicked(Id id, const Rect& rect) {
    const bool inside = hovered(id, rect);
    if (input_.pointerWentDown && inside) {
        store_.setActive(id);
        store_.setFocused(id);
    }
    if (input_.pointerWentUp && inside && store_.active() == id) {
        store_.setActive(0);
        return true;
    }
    return false;
}

bool Context::activate(Id id) const {
    return focused(id) &&
           (input_.keyWentDown(key::Enter) || input_.keyWentDown(key::Space));
}

bool Context::press(Id id, const Rect& rect) {
    const bool inside = hovered(id, rect);
    if (input_.pointerWentDown && inside) {
        store_.setActive(id);
        store_.setFocused(id);
        return true;
    }
    return false;
}

void Context::drawFocusRing(const Rect& rect, float radius) const {
    const float r = radius < 0.0f ? theme_.radius + 1.0f : radius;
    canvas_.rectOutline(rect.x - 1.5f, rect.y - 1.5f, rect.w + 3.0f, rect.h + 3.0f,
                        theme_.colors.accent, r, theme_.strokes.thick);
}

}  // namespace ui
