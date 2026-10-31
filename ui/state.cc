#include "state.hh"

#include <algorithm>

#include "frame_input.hh"
#include "keys.hh"

namespace ui {

namespace {
Id firstOrZero(const std::vector<Id>& order) {
    return order.empty() ? 0 : order.front();
}
}  // namespace

void StateStore::beginFrame() {
    setHot(0);
    focusOrder_.clear();
}

void StateStore::endFrame(const FrameInput& input) {
    if (input.keyWentDown(key::Escape)) {
        setFocused(0);
        setActive(0);
    }
    if (input.keyWentDown(key::Tab)) {
        focusNext(input.shiftDown);
    }
    if (input.pointerWentUp) {
        setActive(0);
    }
    // A press that no widget claimed blurs the focused control. Widgets that
    // accept the press set active before endFrame runs.
    if (input.pointerWentDown && active_ == 0) {
        setFocused(0);
    }

    // A focused widget that disappeared must not keep focus forever. This is
    // also the first-frame case: the first Tab press has no previous focus.
    if (focused_ != 0 && !focusOrder_.empty() &&
        std::find(focusOrder_.begin(), focusOrder_.end(), focused_) == focusOrder_.end()) {
        setFocused(firstOrZero(focusOrder_));
    }
}

WidgetState& StateStore::widget(Id id) {
    return widgets_[id];
}

TextSlot& StateStore::textSlot(Id id) {
    return text_[id];
}

void StateStore::setHot(Id id) {
    if (hot_ == id) return;
    auto previous = widgets_.find(hot_);
    if (previous != widgets_.end()) previous->second.hot = false;
    hot_ = id;
    if (id != 0) widget(id).hot = true;
}

void StateStore::setActive(Id id) {
    if (active_ == id) return;
    auto previous = widgets_.find(active_);
    if (previous != widgets_.end()) previous->second.active = false;
    active_ = id;
    if (id != 0) widget(id).active = true;
}

void StateStore::setFocused(Id id) {
    if (focused_ == id) return;
    auto previous = widgets_.find(focused_);
    if (previous != widgets_.end()) previous->second.focused = false;
    focused_ = id;
    if (id != 0) widget(id).focused = true;
}

void StateStore::registerFocusable(Id id) {
    if (id == 0) return;
    if (std::find(focusOrder_.begin(), focusOrder_.end(), id) == focusOrder_.end()) {
        focusOrder_.push_back(id);
    }
}

void StateStore::focusNext(bool backward) {
    if (focusOrder_.empty()) return;
    const std::size_t n = focusOrder_.size();
    auto it = std::find(focusOrder_.begin(), focusOrder_.end(), focused_);
    if (it == focusOrder_.end()) {
        setFocused(backward ? focusOrder_.back() : focusOrder_.front());
        return;
    }
    const std::size_t index = static_cast<std::size_t>(it - focusOrder_.begin());
    const std::size_t next = backward ? (index + n - 1) % n : (index + 1) % n;
    setFocused(focusOrder_[next]);
}

void StateStore::clear() {
    widgets_.clear();
    text_.clear();
    focusOrder_.clear();
    hot_ = 0;
    active_ = 0;
    focused_ = 0;
}

}  // namespace ui
