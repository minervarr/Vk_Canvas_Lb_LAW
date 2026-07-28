#include "frame_input.hh"
#include "keys.hh"

#include <algorithm>

bool FrameInput::keyWentDown(int keyCode) const {
  return std::find(keysWentDown.begin(), keysWentDown.end(), keyCode) != keysWentDown.end();
}

void FrameInput::beginFrame() {
  pointerWentDown = false;
  pointerWentUp = false;
  wheelDelta = 0.0f;
  keysWentDown.clear();
  typedChars.clear();
  typedCodepoints.clear();
}

void FrameInput::onPointer(const PointerEvent& e) {
  pointerX = e.x;
  pointerY = e.y;
  switch (e.action) {
    case PointerAction::Down:
      if (e.button == 0) {
        pointerDown = true;
        pointerWentDown = true;
      }
      break;
    case PointerAction::Up:
      if (e.button == 0) {
        pointerDown = false;
        pointerWentUp = true;
      }
      break;
    case PointerAction::Move:
    case PointerAction::Enter:
    case PointerAction::Leave:
      break;
  }
}

void FrameInput::onWheel(const WheelEvent& e) {
  wheelDelta += e.deltaY;
}

void FrameInput::onKey(const KeyEvent& e) {
  if (e.down) keysWentDown.push_back(e.keyCode);
  if (e.keyCode == key::Control) ctrlDown = e.down;
  if (e.keyCode == key::Shift)   shiftDown = e.down;
}

void FrameInput::onChar(const CharEvent& e) {
  if (e.codepoint >= 0x20 && e.codepoint <= 0x7E) {
    typedChars += (char)e.codepoint;
  }
  if (e.codepoint >= 0x20 && e.codepoint != 0x7F) {
    typedCodepoints += (char32_t)e.codepoint;
  }
}
