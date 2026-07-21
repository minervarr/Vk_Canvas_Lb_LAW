#pragma once
// Platform seam for mouse/keyboard input, alongside platform.hh's rendering
// seams. Core defines the interface; each platform backend translates its own
// native events into these POD structs and calls into an InputSink. Never
// include platform SDK headers here (see platform.hh's own rule).
//
// Key codes live in the Win32 VK_* numeric space, now named in keys.hh: the
// Win32 backend passes native codes through unmodified, the Wayland backend
// (platform/linux/wayland_display.cc) maps xkb keysyms into the same values.
// Consumers compare against key::* from keys.hh, never platform constants.

#include <cstdint>

enum class PointerAction { Down, Up, Move, Enter, Leave };

struct PointerEvent {
  PointerAction action;
  float x = 0, y = 0;      // screen/window pixels, y-down
  int   button = 0;        // 0 = left, 1 = right, 2 = middle; unused for Move/Enter/Leave
};

struct WheelEvent {
  float x = 0, y = 0;      // pointer position at the time of the wheel event
  float deltaY = 0;        // positive = scroll up/away from user
};

struct KeyEvent {
  int  keyCode = 0;        // platform virtual-key code (Win32 VK_* today)
  bool down = false;       // true on key-down (incl. auto-repeat), false on key-up
};

// Translated text entry (Win32 WM_CHAR today; Android IME commits would map
// here). Distinct from KeyEvent: keyCode is the physical/virtual key, this is
// the character the platform's layout/IME produced. Control characters
// (backspace, enter, ...) arrive as their ASCII control codes — consumers that
// only want printable text must filter (see FrameInput).
struct CharEvent {
  uint32_t codepoint = 0;
};

struct InputSink {
  virtual void onPointer(const PointerEvent&) = 0;
  virtual void onWheel(const WheelEvent&) = 0;
  virtual void onKey(const KeyEvent&) = 0;
  // Default no-op (not pure) so existing sinks that predate text entry keep
  // compiling unchanged.
  virtual void onChar(const CharEvent&) {}
  virtual ~InputSink() = default;
};
