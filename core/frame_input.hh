#pragma once
#include <string>
#include <vector>

#include "input.hh"

// Per-frame edge-detected input state on top of input.hh's event seam.
//
// input.hh delivers raw events (an InputSink gets one call per platform
// message); immediate-mode UI code instead wants to ask, once per frame,
// "did the pointer go down *this frame*?" — every consumer app was
// hand-rolling that accumulation. FrameInput is that accumulator: point the
// platform backend's events at it, call beginFrame() once per frame *before*
// pumping platform messages, then read the fields while building the UI.
//
//   pointerDown           held state, persists across frames
//   pointerWentDown/Up    true only on the transition frame (edge)
//   wheelDelta            sum of this frame's wheel motion (several events
//                         can arrive between two frames), cleared each frame
//   keysWentDown          keycodes whose key-down arrived this frame, in
//                         arrival order — auto-repeat delivers one edge per
//                         repeated key-down (see input.hh), duplicates kept
//   typedChars            printable ASCII characters typed this frame, in
//                         arrival order (several CharEvents can arrive
//                         between two frames: fast typing/paste). Control
//                         chars and non-ASCII are dropped here — handle
//                         those via keysWentDown / your own InputSink
//

// Edges track the primary button/finger only (button 0) — matching the
// touch-first widgets; other buttons still move the pointer position.
struct FrameInput : InputSink {
  float pointerX = 0.0f;
  float pointerY = 0.0f;
  bool pointerDown = false;
  bool pointerWentDown = false;
  bool pointerWentUp = false;
  float wheelDelta = 0.0f;
  std::vector<int> keysWentDown;
  std::string typedChars;

  // True if `keyCode`'s key-down arrived this frame.
  bool keyWentDown(int keyCode) const;

  // Clears the per-frame edges/accumulators; held state persists.
  void beginFrame();

  // InputSink — the platform backend calls these per event.
  void onPointer(const PointerEvent&) override;
  void onWheel(const WheelEvent&) override;
  void onKey(const KeyEvent&) override;
  void onChar(const CharEvent&) override;
};
