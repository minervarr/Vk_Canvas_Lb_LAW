#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "id.hh"
#include "widgets.hh"   // TextFieldState

struct FrameInput;

namespace ui {

// Toolkit-owned interaction state for one widget. Apps own their values
// (a bool, a selected index, a scroll model); this is the state every screen
// used to hand-thread through every call.
struct WidgetState {
    bool  hot    = false;
    bool  active = false;
    bool  focused = false;
    float scrollPx = 0.0f;
};

// Per-frame text-edit storage. `initialized` lets the context adopt the app's
// initial string exactly once without copying it in and out every frame.
struct TextSlot {
    widgets::TextFieldState state;
    bool initialized = false;
};

// Persistent store for one screen lifetime. No Vulkan, no window: a CPU-only
// test can drive it with a FrameInput and inspect every transition.
class StateStore {
public:
    // Clears this frame's hot flags and focus-order registrations. Held state
    // (active, focused, scroll, text) survives.
    void beginFrame();

    // Applies Tab/Shift+Tab focus movement, Escape blur and pointer-release
    // cleanup. Call once after the screen has drawn its widgets.
    void endFrame(const FrameInput& input);

    WidgetState& widget(Id id);
    TextSlot&   textSlot(Id id);

    Id hot() const     { return hot_; }
    Id active() const  { return active_; }
    Id focused() const { return focused_; }

    void setHot(Id id);
    void setActive(Id id);
    void setFocused(Id id);

    // Registers a widget in this frame's Tab order (registration order).
    void registerFocusable(Id id);

    // Advances by one in the current frame's focus order. `backward` follows
    // Shift+Tab. No-op when nothing registered.
    void focusNext(bool backward);

    // Drops every entry. Not needed per frame; useful for a screen reset.
    void clear();

private:
    std::unordered_map<Id, WidgetState> widgets_;
    std::unordered_map<Id, TextSlot>    text_;
    std::vector<Id>                     focusOrder_;
    Id hot_ = 0;
    Id active_ = 0;
    Id focused_ = 0;
};

}  // namespace ui
