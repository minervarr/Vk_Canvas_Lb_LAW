// Asserts must stay live even though the app builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>

#include "touch_hover.hh"

// The contact itself is not a sample. AndroidHost sends onMouseMove for it
// before the press, so these cases are only what happens afterwards. Slop is
// the host's 24 px.

static TouchHoverSample at(float x, float y, bool dragging, bool lift) {
    return TouchHoverSample{ true, dragging, x, y, 0.0f, 0.0f, 24.0f, lift };
}

int main() {
    // No finger: a stray sample must not light or clear anything.
    {
        const auto d = touchHoverFor(
            TouchHoverSample{ false, false, 10, 10, 0, 0, 24, false });
        assert(d.hover == TouchHover::Nothing);
        assert(!d.becameDrag);
    }

    // Still a tap. The point may already be outside the control that was
    // touched — that is the app's hit test. This layer's job is to keep
    // reporting the finger so the control can unlight before the lift.
    {
        const auto origin = touchHoverFor(at(0, 0, false, false));
        assert(origin.hover == TouchHover::Move);
        assert(!origin.becameDrag);

        const auto inside = touchHoverFor(at(10, 0, false, false));
        assert(inside.hover == TouchHover::Move);
        assert(!inside.becameDrag);

        // Exactly on the slop circle is still a tap, in every direction.
        const auto edge = touchHoverFor(at(-24, 0, false, false));
        assert(edge.hover == TouchHover::Move);
        assert(!edge.becameDrag);

        const auto diagonal = touchHoverFor(at(10, 10, false, false));
        assert(diagonal.hover == TouchHover::Move);
        assert(!diagonal.becameDrag);
    }

    // The sample that crosses the slop drops the highlight and admits the
    // drag. It must not also be a move: that would light whatever the finger
    // happens to be over at the moment scrolling begins.
    {
        const auto past = touchHoverFor(at(25, 0, false, false));
        assert(past.hover == TouchHover::Leave);
        assert(past.becameDrag);

        const auto diag = touchHoverFor(at(20, 20, false, false));
        assert(diag.hover == TouchHover::Leave);
        assert(diag.becameDrag);

        const auto up = touchHoverFor(at(0, -30, false, false));
        assert(up.hover == TouchHover::Leave);
        assert(up.becameDrag);
    }

    // A scroll already underway does not re-light and does not leave again.
    {
        const auto scrolling = touchHoverFor(at(80, 40, true, false));
        assert(scrolling.hover == TouchHover::Nothing);
        assert(!scrolling.becameDrag);
    }

    // A lift drops the highlight whether or not the stroke had become a
    // scroll, and it is not the start of a drag — even when the finger comes
    // up outside the slop without a move sample in between.
    {
        const auto tap = touchHoverFor(at(4, 4, false, true));
        assert(tap.hover == TouchHover::Leave);
        assert(!tap.becameDrag);

        const auto flung = touchHoverFor(at(400, -20, false, true));
        assert(flung.hover == TouchHover::Leave);
        assert(!flung.becameDrag);

        const auto dragged = touchHoverFor(at(80, 40, true, true));
        assert(dragged.hover == TouchHover::Leave);
        assert(!dragged.becameDrag);
    }

    // A lift with no finger down is not a leave. Cancelling a gesture the
    // host was not tracking must not wipe a highlight some other pointer owns.
    {
        const auto d = touchHoverFor(
            TouchHoverSample{ false, false, 0, 0, 0, 0, 24, true });
        assert(d.hover == TouchHover::Nothing);
        assert(!d.becameDrag);
    }

    return 0;
}
