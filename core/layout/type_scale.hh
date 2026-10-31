#pragma once
#include "layout.hh"  // UiScale

// The one type scale. Text sizes and geometry derive from a SINGLE factor
// anchored at the app's real render height.
//
// The smallest role is PINNED to the app's legibility floor (the font's own
// geometric minimum, which the app measures from the fonts it shipped). Every
// other role is that floor times kUiTypeRatio^n. Because all roles derive from
// one clamped value, the scale clamps UNIFORMLY below the reference height
// instead of the smallest role clamping alone and squashing the hierarchy
// flat.
//
// The floor is an argument, not a build-generated constant: App_shell stays
// dependent on base modules only and the app owns its font measurement. See
// docs/decisions/0009-text-fit-and-type-scale.md.
//
// The caller passes the window's SHORT SIDE — min(width, height) — not its
// height. This is the same thing while every window is wider than tall; it
// stops being the same the moment a vertical layout exists: a 1080x1920
// monitor would otherwise scale 1.78x and a 1080x2400 phone 2.22x, inflating
// every bar because the screen is TALL, not because it is big.

inline constexpr float kUiReferenceHeight = 1080.0f;
inline constexpr float kUiTypeRatio       = 1.18f;

struct UiTextSizes {
    float caption;    // badges, hints, placeholders, section captions
    float secondary;  // artist, time, duration, search text
    float body;       // grid titles, track titles, nav, settings rows
    float title;      // now-playing title, album-view title
    float header;     // page + panel headers
};

// caption and secondary share a size deliberately — they are separated by the
// text-color ladder and by font style, not by size.

struct UiMetrics {
    float       scale = 1.0f;   // 1.0 at H <= 1080, H/1080 above
    UiTextSizes text{};

    // Spacing/size: pads, gaps, row heights, widths, icon boxes.
    float space(float authored) const { return authored * scale; }

    // Stroke weight: hairlines, borders, bars. Snapped to whole device pixels
    // so a 1px rule never lands blurred across two pixels on a taller display
    // (continuous scaling would put it at 1.33px on a 1440p screen).
    float stroke(float authored) const;
};

// `contentShortSide` is min(width, height); `minReadablePx` is the app's
// font-derived legibility floor. Pinned in ui_metrics_test.
UiMetrics computeUiMetrics(float contentShortSide, float minReadablePx);
