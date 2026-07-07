// gesture.cc — platform-agnostic touch gesture recognizer for the canvas engine.
//
// Copyright (C) 2026 nava.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version. This program is distributed WITHOUT ANY WARRANTY; see the GNU
// Affero General Public License for more details.
#include "gesture.hh"

#include <cmath>

void GestureRecognizer::onTouch(int action, float x, float y, double t) {
    switch (action) {
        case DOWN:
            down_    = true;
            panning_ = false;
            startX_  = lastX_ = x;
            startY_  = lastY_ = y;
            lastT_   = downT_ = t;
            velX_    = velY_ = 0.0f;
            break;

        case MOVE: {
            if (!down_) return;
            double dt = t - lastT_;
            if (dt < 1e-4) dt = 1e-4;
            // Exponentially-smoothed velocity so a single jittery sample at
            // release doesn't dominate the fling decision.
            float vx = static_cast<float>((x - lastX_) / dt);
            float vy = static_cast<float>((y - lastY_) / dt);
            velX_ = 0.7f * velX_ + 0.3f * vx;
            velY_ = 0.7f * velY_ + 0.3f * vy;

            float totalDx = x - startX_;
            float totalDy = y - startY_;
            if (!panning_ && std::hypot(totalDx, totalDy) > tapSlop_) {
                panning_ = true;
                if (onPanStart) onPanStart(startX_, startY_);
            }
            if (panning_ && onPanMove)
                onPanMove(x - lastX_, y - lastY_, totalDx, totalDy);

            lastX_ = x;
            lastY_ = y;
            lastT_ = t;
            break;
        }

        case UP:
            if (down_) {
                if (!panning_ && onLongPress && (t - downT_) >= longPressTime_)
                    onLongPress(x, y);   // fires before onRelease; host consumes it
                if (panning_) { if (onPanEnd) onPanEnd(velX_, velY_); }
                else          { if (onTap)    onTap(x, y); }
                // Always report the lift, after onPanEnd, so a host can do
                // release-based hit-testing (and tell a committed pan from a tap
                // via wasPan) regardless of which branch above ran.
                if (onRelease) onRelease(x, y, panning_);
            }
            down_ = panning_ = false;
            break;

        case CANCEL:
        default:
            if (down_ && panning_ && onPanEnd) onPanEnd(0.0f, 0.0f);
            down_ = panning_ = false;
            break;
    }
}
