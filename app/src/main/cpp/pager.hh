// pager.hh — horizontally swipeable page model for the canvas engine.
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
// Pager tracks a horizontal scroll position across N full-width pages: drag to
// move, release to settle (snap by distance or fling velocity) with a smooth,
// framerate-independent animation. It owns no drawing of page content — the
// host draws each page into pageRect(i) — but it can draw the dot indicator.
// Feed it from a GestureRecognizer (dragStart/dragTo/dragEnd) and tick update().
// ---------------------------------------------------------------------------
#pragma once
#include "canvas.hh"

class Pager {
public:
    void setCount(int n)                  { count_ = (n < 1) ? 1 : n; }
    void setViewport(float w, float h)    { width_ = w; height_ = h; }

    int   count()     const { return count_; }
    float scrollPx()  const { return scroll_; }
    bool  animating() const { return animating_; }
    int   current()   const;                 // nearest settled page index

    // Drive from a gesture. totalDx is pointer-x minus press-x (px).
    void dragStart();
    void dragTo(float totalDx);
    void dragEnd(float velocityX);

    // Advance the settle animation by dt seconds. No-op when not animating.
    void update(float dt);

    // On-screen rect of page i at the current scroll position.
    Rect pageRect(int i) const;

    // Draw a row of dots centred at (cx, cy); the active dot follows the scroll
    // position continuously (so it brightens mid-swipe).
    void drawDots(Canvas& c, float cx, float cy, float spacing, float radius,
                  Color on, Color off) const;

private:
    int   count_     = 1;
    float width_     = 0, height_ = 0;
    float scroll_    = 0;   // px, 0 = page 0 fully shown
    float target_    = 0;   // px, settle goal
    float dragStart_ = 0;   // scroll_ captured at dragStart()
    float animFrom_    = 0; // scroll_ at the start of the settle tween
    float animElapsed_ = 0; // seconds elapsed into the tween
    float animDur_     = 0; // tween duration (s)
    bool  dragging_  = false;
    bool  animating_ = false;

    float clampScroll(float s) const;
};
