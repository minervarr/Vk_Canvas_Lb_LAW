// Asserts must stay live even though the library builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>

#include "pager.hh"

static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
    // ── Count and viewport basics ──────────────────────────────────────────
    {
        Pager p;
        assert(p.count() == 1);
        p.setCount(0);
        assert(p.count() == 1);
        p.setCount(3);
        p.setViewport(100.0f, 50.0f);
        assert(p.count() == 3);
        assert(p.current() == 0);
        assert(!p.animating());
        assert(near(p.scrollPx(), 0.0f));

        Rect r0 = p.pageRect(0);
        assert(near(r0.x, 0.0f) && near(r0.w, 100.0f) && near(r0.h, 50.0f));
        Rect r1 = p.pageRect(1);
        assert(near(r1.x, 100.0f) && near(r1.w, 100.0f));
    }

    // ── Drag clamps to the page range and reports the nearest page ─────────
    {
        Pager p;
        p.setCount(3);
        p.setViewport(100.0f, 50.0f);

        p.dragStart();
        p.dragTo(50.0f);      // dragging right at the first page: clamped to 0
        assert(near(p.scrollPx(), 0.0f));

        p.dragTo(-30.0f);
        assert(near(p.scrollPx(), 30.0f));
        assert(p.current() == 0);

        p.dragTo(-120.0f);
        assert(near(p.scrollPx(), 120.0f));
        assert(p.current() == 1);

        p.dragTo(-1000.0f);   // clamped to the last page
        assert(near(p.scrollPx(), 200.0f));
        assert(p.current() == 2);
    }

    // ── A one-page drag settles to the neighbouring page ───────────────────
    {
        Pager p;
        p.setCount(3);
        p.setViewport(100.0f, 50.0f);

        p.dragStart();
        p.dragTo(-30.0f);     // past the quarter-page commit distance
        p.dragEnd(0.0f);
        assert(p.animating());

        p.update(0.11f);      // halfway through the 0.22 s settle
        assert(p.scrollPx() > 30.0f && p.scrollPx() < 100.0f);

        p.update(0.11f);
        assert(near(p.scrollPx(), 100.0f));
        assert(p.current() == 1);
        assert(!p.animating());
    }

    // ── A fling commits even when the distance is under a quarter page ─────
    {
        Pager p;
        p.setCount(3);
        p.setViewport(100.0f, 50.0f);

        p.dragStart();
        p.dragTo(-10.0f);
        p.dragEnd(-800.0f);   // leftward fling
        p.update(0.22f);
        assert(near(p.scrollPx(), 100.0f));
        assert(p.current() == 1);
    }

    // ── A small drag with no fling returns to the same page ────────────────
    {
        Pager p;
        p.setCount(3);
        p.setViewport(100.0f, 50.0f);

        p.dragStart();
        p.dragTo(-10.0f);
        p.dragEnd(0.0f);
        p.update(0.22f);
        assert(near(p.scrollPx(), 0.0f));
        assert(p.current() == 0);
    }

    // ── update() is a no-op when no settle is running ──────────────────────
    {
        Pager p;
        p.setCount(2);
        p.setViewport(100.0f, 50.0f);
        p.update(10.0f);
        assert(near(p.scrollPx(), 0.0f));
        assert(!p.animating());
    }

    // ── Degenerate viewport: no division by zero, current() stays 0 ────────
    {
        Pager p;
        p.setCount(3);
        p.setViewport(0.0f, 0.0f);
        assert(p.current() == 0);
        p.dragStart();
        p.dragTo(-100.0f);
        assert(near(p.scrollPx(), 0.0f));
        p.dragEnd(-800.0f);
        p.update(1.0f);
        assert(near(p.scrollPx(), 0.0f));
    }

    std::printf("pager_test: all assertions passed\n");
    return 0;
}
