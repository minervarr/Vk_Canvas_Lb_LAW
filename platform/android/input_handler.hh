#pragma once
#include <android/input.h>

class TextBuffer;
class UndoRedo;

class InputHandler {
public:
    void init(float screen_h, float line_height);

    bool handle_key  (AInputEvent* ev, TextBuffer& buf, UndoRedo& undo);
    bool handle_touch(AInputEvent* ev, TextBuffer& buf, float& scroll_y,
                      bool& want_open, bool& want_save, bool& was_tap);

private:
    float screen_h_    = 0.f;
    float line_height_ = 0.f;

    // Touch gesture state (instance — not static — so multiple editors coexist)
    float touch_prev_y_ = 0.f;
    float touch_down_y_ = 0.f;
    float last_tap_x_   = 0.f;
    float last_tap_y_   = 0.f;
    bool  dragging_     = false;

public:
    float last_tap_x() const { return last_tap_x_; }
    float last_tap_y() const { return last_tap_y_; }
private:
};
