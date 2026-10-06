#pragma once

// What one touch sample means for the pointer highlight.
//
// A finger is a pointer only while the stroke is still a tap. Contact itself
// is reported by the host as onMouseMove before the press, so the control
// under the finger lights immediately. Samples after that keep following the
// finger — including a slide that has already left the control but not yet
// crossed the slop — so the control unlights while the finger is still down.
//
// Two samples are a leave. The finger lifting (or the system cancelling the
// gesture) means there is no pointer left on the glass. Crossing the slop
// means the stroke is a scroll, and a scroll is not a point: lighting every
// row the finger passes is how a dragged list flickers. Further samples of a
// scroll say nothing, so a highlight that already dropped stays dropped.
//
// Sampling only at contact is the bug this exists to make unrepresentable.
// The control under the finger at the down edge stayed grey after the finger
// had slid off it and lifted, until the next contact happened to move the
// pointer. The wheel, the click and the drag summary are the host's; this
// answers only the highlight.
enum class TouchHover {
    Nothing,  // no finger, or a scroll already underway
    Move,     // still a tap: the highlight follows this point
    Leave,    // drop every highlight
};

struct TouchHoverSample {
    bool  down;       // a finger was already on the glass before this sample
    bool  dragging;   // the stroke had already crossed the slop
    float x, y;
    float originX, originY;
    float slopPx;
    bool  lift;       // this sample is an up or a cancel
};

struct TouchHoverDecision {
    TouchHover hover;
    // This sample is the one that crossed the slop. The host spends the slop
    // here (the scroll starts where the gesture was admitted, not back at the
    // contact). A lift is never this, even when it lands outside the slop:
    // the finger leaving is not the start of a drag.
    bool becameDrag;
};

inline TouchHoverDecision touchHoverFor(const TouchHoverSample& s) {
    if (!s.down) return { TouchHover::Nothing, false };
    if (s.lift)  return { TouchHover::Leave, false };
    if (s.dragging) return { TouchHover::Nothing, false };
    const float dx = s.x - s.originX;
    const float dy = s.y - s.originY;
    // On the slop circle the stroke is still a tap. The same `<=` the host
    // used when it measured with a square root, so a finger resting at
    // exactly 24 px does not flip between the two.
    if (dx * dx + dy * dy <= s.slopPx * s.slopPx)
        return { TouchHover::Move, false };
    return { TouchHover::Leave, true };
}
