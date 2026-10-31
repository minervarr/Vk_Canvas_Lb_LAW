// Asserts must stay live even though the library builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "gesture.hh"

static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
    // ── A tap under the slop: onTap + onRelease(false), no pan ─────────────
    {
        GestureRecognizer g;
        int taps = 0, releases = 0;
        bool releaseWasPan = true;
        g.onTap = [&](float x, float y) {
            ++taps;
            assert(near(x, 12.0f) && near(y, 22.0f));
        };
        g.onRelease = [&](float, float, bool wasPan) {
            ++releases;
            releaseWasPan = wasPan;
        };
        g.onPanStart = [&](float, float) { assert(false && "tap must not pan"); };

        g.onTouch(GestureRecognizer::DOWN, 10.0f, 20.0f, 0.0);
        g.onTouch(GestureRecognizer::MOVE, 12.0f, 22.0f, 0.05);  // under 16px slop
        g.onTouch(GestureRecognizer::UP, 12.0f, 22.0f, 0.10);

        assert(taps == 1);
        assert(releases == 1);
        assert(!releaseWasPan);
    }

    // ── Crossing the slop starts exactly one pan and suppresses the tap ────
    {
        GestureRecognizer g;
        int panStarts = 0, panMoves = 0, panEnds = 0, taps = 0, releases = 0;
        bool releaseWasPan = false;
        float endVx = 0.0f;
        g.onPanStart = [&](float x, float y) {
            ++panStarts;
            assert(near(x, 0.0f) && near(y, 0.0f));
        };
        g.onPanMove = [&](float, float, float totalDx, float) {
            ++panMoves;
            assert(totalDx > 16.0f);
        };
        g.onPanEnd = [&](float vx, float) {
            ++panEnds;
            endVx = vx;
        };
        g.onTap = [&](float, float) { ++taps; };
        g.onRelease = [&](float, float, bool wasPan) {
            ++releases;
            releaseWasPan = wasPan;
        };

        g.onTouch(GestureRecognizer::DOWN, 0.0f, 0.0f, 0.0);
        g.onTouch(GestureRecognizer::MOVE, 20.0f, 0.0f, 0.1);
        g.onTouch(GestureRecognizer::MOVE, 30.0f, 0.0f, 0.2);
        g.onTouch(GestureRecognizer::UP, 30.0f, 0.0f, 0.3);

        assert(panStarts == 1);
        assert(panMoves == 2);
        assert(panEnds == 1);
        assert(taps == 0);
        assert(releases == 1);
        assert(releaseWasPan);
        // Smoothed velocity is positive for rightward motion.
        assert(endVx > 0.0f);
    }

    // ── Cancel ends a pan with zero velocity and does not report a lift ────
    {
        GestureRecognizer g;
        int panEnds = 0, releases = 0;
        float endVx = 1.0f, endVy = 1.0f;
        g.onPanEnd = [&](float vx, float vy) {
            ++panEnds;
            endVx = vx;
            endVy = vy;
        };
        g.onRelease = [&](float, float, bool) { ++releases; };

        g.onTouch(GestureRecognizer::DOWN, 0.0f, 0.0f, 0.0);
        g.onTouch(GestureRecognizer::MOVE, 20.0f, 0.0f, 0.1);
        g.onTouch(GestureRecognizer::CANCEL, 20.0f, 0.0f, 0.2);

        assert(panEnds == 1);
        assert(near(endVx, 0.0f) && near(endVy, 0.0f));
        assert(releases == 0);
    }

    // ── Long press fires before the lift; a short press does not ────────────
    {
        GestureRecognizer g;
        std::vector<std::string> events;
        g.onLongPress = [&](float, float) { events.push_back("long"); };
        g.onTap = [&](float, float) { events.push_back("tap"); };
        g.onRelease = [&](float, float, bool) { events.push_back("release"); };

        g.onTouch(GestureRecognizer::DOWN, 5.0f, 5.0f, 0.0);
        g.onTouch(GestureRecognizer::UP, 5.0f, 5.0f, 0.60);

        assert(!events.empty());
        assert(events.front() == "long");
        assert(events.back() == "release");
        // Current behaviour also emits onTap after onLongPress; hosts that
        // consume the long press ignore it via their own flag. This test pins
        // the ordering that matters (long press before release), not that
        // implementation detail.
    }
    {
        GestureRecognizer g;
        int longs = 0, taps = 0;
        g.onLongPress = [&](float, float) { ++longs; };
        g.onTap = [&](float, float) { ++taps; };
        g.onTouch(GestureRecognizer::DOWN, 5.0f, 5.0f, 0.0);
        g.onTouch(GestureRecognizer::UP, 5.0f, 5.0f, 0.20);
        assert(longs == 0);
        assert(taps == 1);
    }

    // ── Custom slop is honoured; a move beyond it pans ─────────────────────
    {
        GestureRecognizer g;
        g.setTapSlop(2.0f);
        int panStarts = 0;
        g.onPanStart = [&](float, float) { ++panStarts; };
        g.onTouch(GestureRecognizer::DOWN, 0.0f, 0.0f, 0.0);
        g.onTouch(GestureRecognizer::MOVE, 3.0f, 0.0f, 0.1);
        assert(panStarts == 1);
    }

    std::printf("gesture_test: all assertions passed\n");
    return 0;
}
