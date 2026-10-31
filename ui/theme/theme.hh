#pragma once
#include "type_scale.hh"
#include "types.hh"

// Toolkit theme: colours, spacing, strokes, density and the type scale.
//
// This layer exists so an app does not author a palette and a type ladder as
// magic numbers. It is data, not a scene graph: widgets take their style structs
// and a `ui::Context` carries the theme. The stock `col::` constants remain the
// default look for the legacy `widgets::` API; new UI code uses this header.
//
// No Vulkan, no platform types: safe for a CPU-only build.

namespace ui {

struct Colors {
    Color background = {0.000f, 0.000f, 0.000f, 1.0f};
    Color panel      = {0.110f, 0.110f, 0.140f, 1.0f};
    Color panel2     = {0.150f, 0.150f, 0.190f, 1.0f};
    Color primary    = {0.960f, 0.960f, 0.980f, 1.0f};  // body text
    Color secondary  = {0.667f, 0.667f, 0.667f, 1.0f};  // 170/255
    Color dim        = {0.502f, 0.502f, 0.502f, 1.0f};  // 128/255
    Color accent     = {0.300f, 0.550f, 0.950f, 1.0f};
    Color onAccent   = {1.000f, 1.000f, 1.000f, 1.0f};  // text/ink on `accent`
    Color track      = {0.220f, 0.220f, 0.280f, 1.0f};
    Color thumb      = {0.820f, 0.840f, 0.900f, 1.0f};
    Color danger     = {0.950f, 0.250f, 0.350f, 1.0f};
};

inline constexpr Colors kDarkColors{};

inline constexpr Colors lightColors() {
    Colors c;
    c.background = {0.965f, 0.965f, 0.975f, 1.0f};
    c.panel      = {0.925f, 0.925f, 0.940f, 1.0f};
    c.panel2     = {0.890f, 0.890f, 0.910f, 1.0f};
    c.primary    = {0.080f, 0.080f, 0.100f, 1.0f};
    c.secondary  = {0.320f, 0.320f, 0.360f, 1.0f};
    c.dim        = {0.480f, 0.480f, 0.520f, 1.0f};
    c.accent     = {0.180f, 0.420f, 0.880f, 1.0f};
    c.onAccent   = {1.000f, 1.000f, 1.000f, 1.0f};
    c.track      = {0.860f, 0.860f, 0.890f, 1.0f};
    c.thumb      = {0.250f, 0.260f, 0.300f, 1.0f};
    c.danger     = {0.820f, 0.150f, 0.230f, 1.0f};
    return c;
}

inline constexpr Colors kLightColors = lightColors();

// A 4px base unit at density 1.0. `scaled()` applies density; the type scale is
// applied separately by `Context::space()`, so authored values stay readable.
struct Spacing {
    float unit = 4.0f;
    float xxs  = unit * 0.5f;
    float xs   = unit * 1.0f;
    float sm   = unit * 2.0f;
    float md   = unit * 3.0f;
    float lg   = unit * 4.0f;
    float xl   = unit * 6.0f;

    Spacing scaled(float factor) const {
        Spacing s;
        s.unit = unit * factor;
        s.xxs  = xxs * factor;
        s.xs   = xs * factor;
        s.sm   = sm * factor;
        s.md   = md * factor;
        s.lg   = lg * factor;
        s.xl   = xl * factor;
        return s;
    }
};

struct Strokes {
    float hairline = 1.0f;
    float thin     = 1.0f;
    float thick    = 2.0f;
};

struct Theme {
    Colors  colors{};
    Spacing spacing{};
    Strokes strokes{};
    float   radius  = 8.0f;
    float   density = 1.0f;   // 0.85 compact, 1.0 normal, 1.15 comfortable

    // Density is a token multiplier, not a global constant: a caller can swap
    // compact/comfortable themes without re-authoring every spacing value.
    Spacing scaledSpacing() const { return spacing.scaled(density); }

    Theme withDensity(float d) const {
        Theme t = *this;
        t.density = d;
        return t;
    }
};

inline constexpr Theme kDarkTheme{};

inline constexpr Theme lightTheme() {
    Theme t;
    t.colors = kLightColors;
    return t;
}

inline constexpr Theme kLightTheme = lightTheme();

// Type hierarchy roles. Sizes come from the one base-layer ladder; the role
// names are the toolkit's contract, the numbers are not.
enum class TypeRole { Caption, Secondary, Body, Title, Header };

// The type ladder is the base module's UiMetrics: the engine owns the maths,
// the app owns the legibility floor measured from its fonts.
using TypeScale = UiMetrics;

inline TypeScale typeScale(float contentShortSide, float minReadablePx) {
    return computeUiMetrics(contentShortSide, minReadablePx);
}

}  // namespace ui
