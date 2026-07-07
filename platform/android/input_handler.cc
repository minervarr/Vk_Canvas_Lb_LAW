#include "input_handler.hh"
#include "text_buffer.hh"
#include "undo_redo.hh"
#include <android/keycodes.h>
#include <cmath>
#include <algorithm>

static char keycode_to_ascii(int32_t keycode, int32_t meta) {
    bool shift = (meta & AMETA_SHIFT_ON) != 0;
    if (keycode >= AKEYCODE_A && keycode <= AKEYCODE_Z) {
        char base = 'a' + (keycode - AKEYCODE_A);
        return shift ? (base - 32) : base;
    }
    if (keycode >= AKEYCODE_0 && keycode <= AKEYCODE_9) {
        static const char digits[]  = "0123456789";
        static const char shifted[] = ")!@#$%^&*(";
        int idx = keycode - AKEYCODE_0;
        return shift ? shifted[idx] : digits[idx];
    }
    switch (keycode) {
        case AKEYCODE_SPACE:         return ' ';
        case AKEYCODE_COMMA:         return shift ? '<' : ',';
        case AKEYCODE_PERIOD:        return shift ? '>' : '.';
        case AKEYCODE_SLASH:         return shift ? '?' : '/';
        case AKEYCODE_SEMICOLON:     return shift ? ':' : ';';
        case AKEYCODE_APOSTROPHE:    return shift ? '"' : '\'';
        case AKEYCODE_LEFT_BRACKET:  return shift ? '{' : '[';
        case AKEYCODE_RIGHT_BRACKET: return shift ? '}' : ']';
        case AKEYCODE_BACKSLASH:     return shift ? '|' : '\\';
        case AKEYCODE_GRAVE:         return shift ? '~' : '`';
        case AKEYCODE_MINUS:         return shift ? '_' : '-';
        case AKEYCODE_EQUALS:        return shift ? '+' : '=';
        case AKEYCODE_TAB:           return '\t';
        default:                     return 0;
    }
}

void InputHandler::init(float screen_h, float line_height) {
    screen_h_    = screen_h;
    line_height_ = line_height;
}

bool InputHandler::handle_key(AInputEvent* ev, TextBuffer& buf, UndoRedo& undo) {
    int32_t action = AKeyEvent_getAction(ev);
    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_MULTIPLE)
        return false;
    int32_t keycode = AKeyEvent_getKeyCode(ev);
    int32_t meta    = AKeyEvent_getMetaState(ev);
    bool    ctrl    = (meta & AMETA_CTRL_ON) != 0;

    if (ctrl) {
        switch (keycode) {
            case AKEYCODE_Z: undo.undo(buf); return true;
            case AKEYCODE_Y: undo.redo(buf); return true;
            default: break;
        }
    }

    switch (keycode) {
        case AKEYCODE_DEL: {
            if (buf.cursor_pos() > 0) {
                size_t pos = buf.cursor_pos();
                undo.record_erase(pos - 1, std::string(1, buf.at(pos - 1)));
                buf.erase_before();
            }
            return true;
        }
        case AKEYCODE_FORWARD_DEL: {
            if (buf.cursor_pos() < buf.length()) {
                size_t pos = buf.cursor_pos();
                undo.record_erase(pos, std::string(1, buf.at(pos)));
                buf.erase_after();
            }
            return true;
        }
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER: {
            size_t pos = buf.cursor_pos();
            if (!undo.try_coalesce_insert(pos, '\n'))
                undo.record_insert(pos, "\n");
            buf.insert('\n');
            return true;
        }
        case AKEYCODE_DPAD_LEFT:  buf.move_cursor(-1); return true;
        case AKEYCODE_DPAD_RIGHT: buf.move_cursor(1);  return true;
        case AKEYCODE_DPAD_UP: {
            int l = buf.cursor_line() - 1;
            if (l >= 0) buf.move_to_line_col(l, buf.cursor_col());
            return true;
        }
        case AKEYCODE_DPAD_DOWN: {
            int l = buf.cursor_line() + 1;
            if (l < buf.line_count()) buf.move_to_line_col(l, buf.cursor_col());
            return true;
        }
        default: break;
    }

    char ch = keycode_to_ascii(keycode, meta);
    if (ch != 0) {
        size_t pos = buf.cursor_pos();
        if (!undo.try_coalesce_insert(pos, ch))
            undo.record_insert(pos, std::string(1, ch));
        buf.insert(ch);
        return true;
    }
    return false;
}

bool InputHandler::handle_touch(AInputEvent* ev, TextBuffer& /*buf*/,
                                 float& scroll_y, bool& want_open,
                                 bool& want_save, bool& was_tap) {
    was_tap = false;
    int32_t action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    float   x      = AMotionEvent_getX(ev, 0);
    float   y      = AMotionEvent_getY(ev, 0);

    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            touch_prev_y_ = y;
            touch_down_y_ = y;
            dragging_     = false;
            return true;
        case AMOTION_EVENT_ACTION_MOVE: {
            float delta = touch_prev_y_ - y;
            if (!dragging_ && fabsf(y - touch_down_y_) > 8.f) dragging_ = true;
            if (dragging_) {
                scroll_y += delta;
                if (scroll_y < 0.f) scroll_y = 0.f;
            }
            touch_prev_y_ = y;
            return true;
        }
        case AMOTION_EVENT_ACTION_UP:
            if (!dragging_) {
                last_tap_x_ = x;
                last_tap_y_ = y;
                was_tap = true;
            }
            dragging_ = false;
            return true;
    }
    return false;
}
