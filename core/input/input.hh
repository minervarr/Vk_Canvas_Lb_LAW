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

#include <cstddef>
#include <cstdint>
#include <string>

enum class PointerAction { Down, Up, Move, Enter, Leave };

struct PointerEvent {
  PointerAction action;
  float x = 0, y = 0;      // screen/window pixels, y-down
  // 0 = left, 1 = right, 2 = middle, 3 = back, 4 = forward; unused for
  // Move/Enter/Leave. 3/4 are the pair of extra buttons on the side of most
  // mice (Win32 XBUTTON1/XBUTTON2, evdev BTN_SIDE/BTN_EXTRA) — the ones a
  // browser binds to back/forward. Numbering follows the platforms' own
  // ordering, so a backend that only knows the first three still maps 0-2 to
  // the same values it always did.
  int   button = 0;
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

// Whole-buffer text replacement from a platform input method.
//
// CharEvent cannot express what an IME does. Composing Korean jamo into a
// syllable, or picking a Chinese candidate over a pinyin reading, *rewrites*
// the pending run rather than appending to it — the text that was on screen a
// moment ago is not a prefix of the text that follows. A stream of "a character
// was typed" events has nowhere to put that.
//
// So the platform reports the field's authoritative contents instead of a
// delta, and the widget adopts them wholesale. This also means the input method
// keeps ownership of composition state, which is the only place it can
// correctly live: reconstructing it on our side would be writing an IME.
//
// Backends without an input method (Win32 WM_CHAR, Wayland xkb) never emit
// this and keep using CharEvent.
struct TextEditEvent {
  std::string text;         // UTF-8, the complete new contents of the field
  size_t cursorByte = 0;    // caret offset in bytes into `text`
};

struct InputSink {
  virtual void onPointer(const PointerEvent&) = 0;
  virtual void onWheel(const WheelEvent&) = 0;
  virtual void onKey(const KeyEvent&) = 0;
  // Default no-op (not pure) so existing sinks that predate text entry keep
  // compiling unchanged.
  virtual void onChar(const CharEvent&) {}
  virtual void onTextEdit(const TextEditEvent&) {}
  virtual ~InputSink() = default;
};
