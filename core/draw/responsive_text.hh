#pragma once
#include <algorithm>

// A text-size role that's a percentage of a reference content height,
// floored at a hard minimum (typically a font's own geometric legibility
// floor — e.g. the font engine's min_text_size tool output) so text scales
// with the window/monitor but never drops below legible. The app defines
// its own set of roles/percentages (what's "body text" vs "header text" is
// app policy) and its own floor (depends on which fonts it ships); this is
// just the scaling formula, reusable across any vk_canvas app.
struct ResponsiveTextScale {
    float floorPx = 0.0f;

    float sizeFor(float pct, float actualHeight) const {
        return std::max(pct * actualHeight, floorPx);
    }
};
