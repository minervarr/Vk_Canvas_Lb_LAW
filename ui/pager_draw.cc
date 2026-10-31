// pager_draw.cc — the drawing half of Pager, split from pager.cc so the page
// model can be tested without Canvas or the font engine.
#include "pager.hh"
#include "canvas.hh"

#include <algorithm>
#include <cmath>

void Pager::drawDots(Canvas& c, float cx, float cy, float spacing, float radius,
                     Color on, Color off) const {
    float pos = (width_ > 0.0f) ? scroll_ / width_ : 0.0f;  // fractional page
    float x0  = cx - (static_cast<float>(count_ - 1) * 0.5f) * spacing;
    for (int i = 0; i < count_; i++) {
        float t = 1.0f - std::min(std::fabs(pos - static_cast<float>(i)), 1.0f);
        Color col{
            off.r + (on.r - off.r) * t,
            off.g + (on.g - off.g) * t,
            off.b + (on.b - off.b) * t,
            off.a + (on.a - off.a) * t,
        };
        float x = x0 + static_cast<float>(i) * spacing;
        // A rounded square with radius == half-size approximates a dot.
        c.rect(x - radius, cy - radius, radius * 2.0f, radius * 2.0f, col, radius);
    }
}
