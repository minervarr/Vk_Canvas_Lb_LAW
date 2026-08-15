#pragma once
#include "app_view.hh"
#include "frame_input.hh"   // vk_canvas: FrameInput, and input.hh's event structs

// ── An AppView for an immediate-mode application ─────────────────────────────
//
// Host talks in CALLBACKS: "the left button went down at 40,12". An
// immediate-mode UI asks the opposite question, once per frame, while it is
// building itself: "is the pointer inside this rect AND did it go down this
// frame?" Neither shape is wrong; they are two ends of the same events, and
// vk_canvas already ships the accumulator that converts one into the other —
// FrameInput, which IS an InputSink.
//
// So this class is deliberately thin: it implements the AppView half, rebuilds
// the raw event each callback used to carry, and posts it into a FrameInput the
// app reads while drawing. An app built this way inherits every widget in
// vk_canvas's widgets.hh unchanged, because those all take `const FrameInput&`.
//
// Matrix Player does NOT use this — it hit-tests on the callbacks directly, and
// uses no FrameInput anywhere. That is the point of the class existing rather
// than the choice being baked into Host: both styles are legitimate, and which
// one an app wants is not a platform question.
//
//   class MyApp : public FrameInputView {
//       void run() {
//           while (running_) {
//               beginFrame();              // BEFORE the pump, always
//               host_->pump(dirty_);
//               if (input().pointerWentDown && hit(button, input())) ...
//           }
//       }
//   };
//
// Everything AppView declares beyond input stays available to override as
// usual: this class touches only the input half and defaults the rest.
class FrameInputView : public AppView {
public:
    // Clears the per-frame edges (pointerWentDown/Up, wheelDelta, key and
    // character queues) while leaving held state alone.
    //
    // Call it ONCE per frame and BEFORE pump(), never after. Pump is what
    // delivers this frame's events; clearing afterwards discards exactly the
    // events the frame was about to act on, and the symptom is a UI that
    // ignores every second click rather than an obvious failure.
    void beginFrame() { input_.beginFrame(); sawInput_ = false; }

    // Did ANY input reach this view since the last beginFrame()?
    //
    // An event-driven app redraws when something happened and sleeps when
    // nothing did, so it needs to ask that question once a frame. The obvious
    // way is to enumerate the FrameInput fields that could have changed —
    // pointerWentDown, wheelDelta, typedCodepoints, keysWentDown — and that is
    // exactly the shape that breaks, silently, the day a new kind of input
    // exists. Forgetting to extend the list is not a compile error; it is a UI
    // that ignores one kind of input and nothing else.
    //
    // Measured: an input method does not produce typed codepoints at all. It
    // rewrites the whole buffer (see AppView::onTextEditPortable), so a list
    // that named the other four was complete and still missed every keystroke
    // typed on a phone — the screen only caught up when the user happened to
    // tap something.
    //
    // So the view answers instead. Every override below sets this, which means
    // a callback added later is covered by having been written at all.
    bool inputArrived() const { return sawInput_; }

    // Read while building the frame. Non-const so an app may consume an edge
    // it has handled (a common immediate-mode idiom) rather than having every
    // later widget see the same click.
    FrameInput&       input()       { return input_; }
    const FrameInput& input() const { return input_; }

    // ── AppView: the input half ─────────────────────────────────────────────
    //
    // Every one of these marks sawInput_. See inputArrived() for why that is
    // the view's job rather than each consumer's.
    void onMouseMove(int x, int y) override {
        sawInput_ = true;
        input_.onPointer({PointerAction::Move, (float)x, (float)y, 0});
    }
    void onMouseLeave() override {
        sawInput_ = true;
        input_.onPointer({PointerAction::Leave, 0, 0, 0});
    }
    void onLButtonDown(int x, int y) override {
        sawInput_ = true;
        input_.onPointer({PointerAction::Down, (float)x, (float)y, 0});
    }
    void onLButtonUp(int x, int y) override {
        sawInput_ = true;
        input_.onPointer({PointerAction::Up, (float)x, (float)y, 0});
    }
    // A double-click is also a click. FrameInput has no double-click concept,
    // and an app that wants one overrides this and calls the base — dropping
    // it here instead would make the SECOND click of a double vanish, which
    // is a worse default than treating it as an ordinary press.
    void onLButtonDblClk(int x, int y) override { onLButtonDown(x, y); }

    // AppView carries wheel motion in Win32's WHEEL_DELTA units (120 per
    // detent, positive = away from the user); vk_canvas's InputSink convention
    // is +-1 per detent with the same sign. Converting here rather than at
    // either end is what keeps both conventions intact for their own
    // consumers.
    void onMouseWheel(int x, int y, int delta) override {
        sawInput_ = true;
        input_.onWheel({(float)x, (float)y, (float)delta / 120.0f});
    }

    void onKeyDownPortable(int keyCode) override {
        sawInput_ = true;
        input_.onKey({keyCode, /*down=*/true});
    }
    void onKeyUpPortable(int keyCode) override {
        sawInput_ = true;
        input_.onKey({keyCode, /*down=*/false});
    }
    void onCharPortable(uint32_t codepoint) override {
        sawInput_ = true;
        input_.onChar({codepoint});
    }
    void onTextEditPortable(const std::string& text, size_t cursorByte) override {
        sawInput_ = true;
        input_.onTextEdit({text, cursorByte});
    }

    // onDragEnd is deliberately NOT forwarded. It is a SUMMARY of a stroke the
    // down/up edges above already delivered in full, and feeding it in as well
    // would double-count the gesture. An immediate-mode app tracks the drag
    // itself from pointerDown, which is the only way it can draw the
    // in-progress state anyway.

private:
    FrameInput input_;
    bool       sawInput_ = false;
};
