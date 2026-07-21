#pragma once
// Cross-platform key codes for input.hh's KeyEvent::keyCode.
//
// The values are numerically equal to the Win32 VK_* constants, so the Win32
// backend keeps passing native codes through unchanged; the Wayland backend
// maps xkb keysyms into this same space (wayland_display.cc). Consumers
// should compare against these names, never against raw platform constants.
// Letters and digits sit at their ASCII uppercase positions, like VK codes.

namespace key {

constexpr int Backspace = 0x08;
constexpr int Tab       = 0x09;
constexpr int Enter     = 0x0D;
constexpr int Shift     = 0x10;
constexpr int Control   = 0x11;
constexpr int Alt       = 0x12;
constexpr int Escape    = 0x1B;
constexpr int Space     = 0x20;
constexpr int PageUp    = 0x21;
constexpr int PageDown  = 0x22;
constexpr int End       = 0x23;
constexpr int Home      = 0x24;
constexpr int Left      = 0x25;
constexpr int Up        = 0x26;
constexpr int Right     = 0x27;
constexpr int Down      = 0x28;
constexpr int Delete    = 0x2E;

// '0'..'9' and 'A'..'Z' are their ASCII values (0x30..0x39, 0x41..0x5A).
constexpr int Digit0 = 0x30;
constexpr int A      = 0x41;

constexpr int Numpad0 = 0x60;   // ..Numpad9 = 0x69
constexpr int F1      = 0x70;   // ..F12     = 0x7B

}  // namespace key
