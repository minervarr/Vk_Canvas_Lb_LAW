#pragma once
// How a text edge is stored in an 8-bit sRGB swapchain.
//
// msdf_frag.slang is the other copy of these two functions. The shader cannot
// be executed here, so this header is the one a test can pin. Change one,
// change the other.
//
// `text`, `dst` and the result are display-referred sRGB numbers in [0,1],
// the same numbers a Color carries and the same bytes an UNORM swapchain
// stores. `cov` is linear area coverage, 0 = destination only, 1 = text only.
//
// textCoverLinear is what an sRGB attachment view stores: the hardware
// decodes the destination, blends in linear light, and encodes on store.
// That is the path Renderer uses when it can open such a view.
//
// textCoverLegacy is the fallback a plain UNORM attachment is stuck with.
// The source is premultiplied in linear light and then encoded, and the
// destination is attenuated in gamma. On black the two agree. On anything
// else the legacy edge goes pale, loses its hue, and on a light page a
// channel clamps to 1 — a white ring around coloured type.

#include <algorithm>
#include <cmath>

struct TextCoverRgb {
    float r, g, b;
};

inline float textCoverSrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float textCoverLinearToSrgb(float c) {
    return c <= 0.0031308f ? c * 12.92f
                           : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline float textCoverClamp01(float c) {
    return std::clamp(c, 0.0f, 1.0f);
}

inline TextCoverRgb textCoverLinear(TextCoverRgb text, TextCoverRgb dst, float cov) {
    auto ch = [&](float t, float d) {
        float lin = textCoverSrgbToLinear(t) * cov
                  + textCoverSrgbToLinear(d) * (1.0f - cov);
        return textCoverLinearToSrgb(lin);
    };
    return { ch(text.r, dst.r), ch(text.g, dst.g), ch(text.b, dst.b) };
}

inline TextCoverRgb textCoverLegacy(TextCoverRgb text, TextCoverRgb dst, float cov) {
    auto ch = [&](float t, float d) {
        float src = textCoverLinearToSrgb(textCoverSrgbToLinear(t) * cov);
        return textCoverClamp01(src + d * (1.0f - cov));
    };
    return { ch(text.r, dst.r), ch(text.g, dst.g), ch(text.b, dst.b) };
}
