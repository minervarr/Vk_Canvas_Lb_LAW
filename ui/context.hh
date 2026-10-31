#pragma once
#include "canvas.hh"
#include "frame_input.hh"
#include "state.hh"
#include "theme.hh"

// Per-frame immediate-mode UI context.
//
// Construct one at the top of a screen and draw with it:
//
//   ui::Context ctx(canvas, input, store, theme, type);
//   ui::toggle(ctx, "settings.wifi"_id, row, "Wi-Fi", state.wifi);
//
// The context owns no window, device or Canvas. It binds this frame's Canvas
// and FrameInput to the persistent StateStore and the app's theme/type scale,
// so widgets stop threading hot/active/focus/scroll and text-edit state through
// every signature. `beginFrame()` runs in the constructor and `endFrame()` in
// the destructor: create one context per frame, not one per app.
namespace ui {

class Context {
public:
    Context(Canvas& canvas, FrameInput& input, StateStore& store,
            Theme theme = kDarkTheme,
            TypeScale type = typeScale(1080.0f, 16.0f),
            double nowSeconds = 0.0);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Canvas& canvas() { return canvas_; }
    const Canvas& canvas() const { return canvas_; }
    const FrameInput& input() const { return input_; }
    const Theme& theme() const { return theme_; }
    const TypeScale& type() const { return type_; }
    double nowSeconds() const { return nowSeconds_; }

    // Authored spacing × theme density × the screen's type scale.
    float space(float authored) const {
        return authored * theme_.density * type_.scale;
    }

    WidgetState& state(Id id) { return store_.widget(id); }
    TextSlot& textSlot(Id id) { return store_.textSlot(id); }

    bool hot(Id id) const { return store_.hot() == id; }
    bool active(Id id) const { return store_.active() == id; }
    bool focused(Id id) const { return store_.focused() == id; }

    void focus(Id id) { store_.setFocused(id); }
    void registerFocusable(Id id) { store_.registerFocusable(id); }

    // Marks `id` hot when the pointer is inside `rect`. Returns that fact.
    bool hovered(Id id, const Rect& rect);

    // Full press/release click: press inside focuses and activates; release
    // inside is the click. Returns true on the release frame.
    bool clicked(Id id, const Rect& rect);

    // Keyboard activation for controls: Enter/Space while focused.
    bool activate(Id id) const;

    // Pointer press edge used by drag widgets (slider). Focuses and activates.
    bool press(Id id, const Rect& rect);

    // Convenience for drawing a focus ring around a row.
    void drawFocusRing(const Rect& rect, float radius = -1.0f) const;

private:
    Canvas&     canvas_;
    FrameInput& input_;
    StateStore& store_;
    Theme       theme_;
    TypeScale   type_;
    double      nowSeconds_;
};

}  // namespace ui
