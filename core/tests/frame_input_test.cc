// Asserts must stay live even though the library builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "frame_input.hh"

static PointerEvent ptr(PointerAction a, float x, float y, int button = 0) {
  PointerEvent e;
  e.action = a;
  e.x = x;
  e.y = y;
  e.button = button;
  return e;
}

int main() {
  FrameInput in;

  // Frame 1: pointer goes down — edge fires and position updates.
  in.beginFrame();
  in.onPointer(ptr(PointerAction::Down, 10, 20));
  assert(in.pointerWentDown && in.pointerDown);
  assert(in.pointerX == 10 && in.pointerY == 20);

  // Frame 2: pointer stays down — went-down edge must clear, held persists.
  in.beginFrame();
  assert(!in.pointerWentDown);
  assert(in.pointerDown);

  // Frame 3: pointer released.
  in.beginFrame();
  in.onPointer(ptr(PointerAction::Up, 11, 21));
  assert(in.pointerWentUp && !in.pointerDown);

  // Frame 4: went-up edge must clear even though pointerDown stays false.
  in.beginFrame();
  assert(!in.pointerWentUp && !in.pointerDown);

  // Move updates position without touching the edges/held state.
  in.onPointer(ptr(PointerAction::Move, 42, 43));
  assert(in.pointerX == 42 && in.pointerY == 43);
  assert(!in.pointerWentDown && !in.pointerDown);

  // A non-primary button moves the pointer but fires no primary edge.
  in.beginFrame();
  in.onPointer(ptr(PointerAction::Down, 50, 51, /*button=*/1));
  assert(!in.pointerWentDown && !in.pointerDown);
  assert(in.pointerX == 50 && in.pointerY == 51);

  // Wheel: several events between two frames accumulate (mixed directions),
  // then clear on beginFrame().
  in.beginFrame();
  in.onWheel(WheelEvent{0, 0, 1.0f});
  in.onWheel(WheelEvent{0, 0, 1.0f});
  in.onWheel(WheelEvent{0, 0, -0.5f});
  assert(in.wheelDelta == 1.5f);
  in.beginFrame();
  assert(in.wheelDelta == 0.0f);

  // Keys: down events queue this frame's keycodes in arrival order
  // (auto-repeat = one edge per key-down), key-ups don't queue, and the
  // queue clears each frame instead of latching.
  in.beginFrame();
  in.onKey(KeyEvent{65, true});
  in.onKey(KeyEvent{66, true});
  in.onKey(KeyEvent{65, true});  // auto-repeat: a second edge for 65
  in.onKey(KeyEvent{67, false});
  assert(in.keyWentDown(65) && in.keyWentDown(66));
  assert(!in.keyWentDown(67));
  assert(in.keysWentDown.size() == 3);
  assert(in.keysWentDown[0] == 65 && in.keysWentDown[1] == 66 && in.keysWentDown[2] == 65);

  in.beginFrame();
  assert(in.keysWentDown.empty());
  assert(!in.keyWentDown(65));

  // The same key pressed on a later frame fires again (not stuck false).
  in.beginFrame();
  in.onKey(KeyEvent{65, true});
  assert(in.keyWentDown(65));

  printf("frame_input_test: OK\n");
  return 0;
}
