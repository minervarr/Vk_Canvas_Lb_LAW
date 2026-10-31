// gesture.hh — platform-agnostic touch gesture recognizer for the canvas engine.
//
// Copyright (C) 2026 nava.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version. This program is distributed WITHOUT ANY WARRANTY; see the GNU
// Affero General Public License for more details.
//
// ---------------------------------------------------------------------------
// GestureRecognizer turns a stream of single-pointer touch samples into
// high-level gestures (tap, pan, fling). It is intentionally free of any
// Android/JNI types: the host maps its platform touch codes onto onTouch()'s
// action enum and feeds a monotonic timestamp (seconds). Wire the callbacks you
// care about; unset callbacks are simply not invoked.
// ---------------------------------------------------------------------------
#pragma once
#include <functional>

class GestureRecognizer {
public:
    enum Action { DOWN = 0, MOVE = 1, UP = 2, CANCEL = 3 };

    // Fired on a touch that never moved past the tap slop.
    std::function<void(float x, float y)> onTap;
    // Fired once movement first exceeds the slop (drag begins).
    std::function<void(float x, float y)> onPanStart;
    // Fired on every move while dragging. dx/dy are since the last sample;
    // totalDx/totalDy are since the press.
    std::function<void(float dx, float dy, float totalDx, float totalDy)> onPanMove;
    // Fired on release/cancel after a pan, with the final pointer velocity (px/s).
    std::function<void(float velocityX, float velocityY)> onPanEnd;
    // Fired on every release (UP, not CANCEL) at the final pointer position, with
    // wasPan = whether movement passed the slop. Where onTap fires only for a
    // press that never moved (strict/precise), onRelease lets a host resolve a
    // control by the LIFT point — release-based hit-testing that tolerates a
    // finger sliding between press and release. Both fire, so precise and
    // slop-tolerant handling can coexist on the same recognizer.
    std::function<void(float x, float y, bool wasPan)> onRelease;

    // Fired on release of a press held past longPressTime without panning (a long
    // tap), just BEFORE onRelease — a host can consume it (e.g. open a context
    // menu) and have its onRelease handler early-out via its own flag. Default 0.5s.
    std::function<void(float x, float y)> onLongPress;

    void setTapSlop(float px) { tapSlop_ = px; }
    void setLongPressTime(double seconds) { longPressTime_ = seconds; }

    // action: one of Action. t: monotonic time in seconds.
    void onTouch(int action, float x, float y, double t);

private:
    bool   down_    = false;
    bool   panning_ = false;
    float  startX_  = 0, startY_ = 0;
    float  lastX_   = 0, lastY_  = 0;
    double lastT_   = 0;
    double downT_   = 0;
    float  velX_    = 0, velY_   = 0;
    float  tapSlop_ = 16.0f;
    double longPressTime_ = 0.5;
};
