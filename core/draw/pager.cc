// pager.cc — horizontally swipeable page model for the canvas engine.
//
// Copyright (C) 2026 nava.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version. This program is distributed WITHOUT ANY WARRANTY; see the GNU
// Affero General Public License for more details.
#include "pager.hh"

#include <algorithm>
#include <cmath>

float Pager::clampScroll(float s) const {
    float maxScroll = static_cast<float>(count_ - 1) * width_;
    return std::min(std::max(s, 0.0f), maxScroll);
}

int Pager::current() const {
    if (width_ <= 0.0f) return 0;
    int idx = static_cast<int>(std::lround(scroll_ / width_));
    return std::min(std::max(idx, 0), count_ - 1);
}

void Pager::dragStart() {
    dragging_  = true;
    animating_ = false;
    dragStart_ = scroll_;
}

void Pager::dragTo(float totalDx) {
    if (width_ <= 0.0f) return;
    // Dragging left (totalDx < 0) reveals the next page → scroll increases.
    scroll_ = clampScroll(dragStart_ - totalDx);
}

void Pager::dragEnd(float velocityX) {
    dragging_ = false;
    if (width_ <= 0.0f) { animating_ = false; return; }

    int   cur   = static_cast<int>(std::lround(dragStart_ / width_));
    float moved = scroll_ - dragStart_;
    int   target = cur;

    // Commit to a neighbouring page on either a quarter-page drag or a fling.
    const float kDistance = width_ * 0.25f;
    const float kFling    = 700.0f;  // px/s
    if (moved > kDistance || velocityX < -kFling)      target = cur + 1;
    else if (moved < -kDistance || velocityX > kFling) target = cur - 1;

    target = std::min(std::max(target, 0), count_ - 1);
    target_      = static_cast<float>(target) * width_;
    animFrom_    = scroll_;
    animElapsed_ = 0.0f;
    animDur_     = 0.22f;        // snappy, time-bounded settle
    animating_   = true;
}

void Pager::update(float dt) {
    if (!animating_) return;
    animElapsed_ += dt;
    float t = (animDur_ > 0.0f) ? (animElapsed_ / animDur_) : 1.0f;
    if (t >= 1.0f) {
        scroll_    = target_;
        animating_ = false;
        return;
    }
    float inv = 1.0f - t;
    float e   = 1.0f - inv * inv * inv;   // ease-out cubic
    scroll_ = animFrom_ + (target_ - animFrom_) * e;
}

Rect Pager::pageRect(int i) const {
    return Rect{static_cast<float>(i) * width_ - scroll_, 0.0f, width_, height_};
}
