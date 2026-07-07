#pragma once
// Platform seam for mouse/keyboard input, alongside platform.hh's rendering
// seams. Core defines the interface; each platform backend translates its own
// native events into these POD structs and calls into an InputSink. Never
// include platform SDK headers here (see platform.hh's own rule).
//
// Key codes are raw platform virtual-key values passed through unmodified —
// today that means Win32 VK_* constants. A future Linux/Wayland backend would
// map X11/Wayland keysyms into the same int space; unifying that mapping is
// deliberately out of scope until a second platform actually needs it.

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

struct InputSink {
  virtual void onPointer(const PointerEvent&) = 0;
  virtual void onWheel(const WheelEvent&) = 0;
  virtual void onKey(const KeyEvent&) = 0;
  virtual ~InputSink() = default;
};
