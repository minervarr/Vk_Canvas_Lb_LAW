#pragma once
#include "type_scale.hh"
#include "types.hh"

// Toolkit theme skeleton.
//
// The point of this layer is to stop each app authoring a palette and a type
// ladder as magic numbers. It is data, not a scene graph: widgets keep taking
// their style structs, and a consumer can pass these tokens explicitly. The
// stock `col::` constants in canvas.hh remain the default look for existing
// callers; this is the canonical place for new UI code and for a theme that a
// screen can override.
//
// No Vulkan, no platform types: this header is safe for a CPU-only build.

namespace ui {

struct Colors {
    Color background = {0.000f, 0.000f, 0.000f, 1.0f};
    Color panel      = {0.110f, 0.110f, 0.140f, 1.0f};
    Color panel2     = {0.150f, 0.150f, 0.190f, 1.0f};
    Color primary    = {0.960f, 0.960f, 0.980f, 1.0f};  // body text
    Color secondary  = {0.667f, 0.667f, 0.667f, 1.0f};  // 170/255
    Color dim        = {0.502f, 0.502f, 0.502f, 1.0f};  // 128/255
    Color accent     = {0.300f, 0.550f, 0.950f, 1.0f};
    Color track      = {0.220f, 0.220f, 0.280f, 1.0f};
    Color thumb      = {0.820f, 0.840f, 0.900f, 1.0f};
    Color danger     = {0.950f, 0.250f, 0.350f, 1.0f};
};

inline constexpr Colors kDarkColors{};

// A 4px base unit, multiplied by the theme's density and the UiMetrics scale.
struct Spacing {
    float unit = 4.0f;
    float xxs  = unit * 0.5f;
    float xs   = unit * 1.0f;
    float sm   = unit * 2.0f;
    float md   = unit * 3.0f;
    float lg   = unit * 4.0f;
    float xl   = unit * 6.0f;
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
};

inline constexpr Theme kDarkTheme{};

// The type ladder is the base module's UiMetrics: the engine owns the maths,
// the app owns the legibility floor measured from its fonts.
using TypeScale = UiMetrics;

inline TypeScale typeScale(float contentShortSide, float minReadablePx) {
    return computeUiMetrics(contentShortSide, minReadablePx);
}

}  // namespace ui
