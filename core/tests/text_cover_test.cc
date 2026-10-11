// The white ring around coloured type is textCoverLegacy, not the atlas.
// Pink over a light page clamps a channel to white; the same coverage
// blended in linear light stays pink. Light type on black must stay heavier
// than a gamma SRC_ALPHA blend, which is why the fallback still premultiplies
// in linear light instead of mixing the sRGB numbers directly.
#undef NDEBUG
#include "text_cover.hh"

#include <cassert>
#include <cmath>
#include <cstdlib>

static int byte8(float c) {
    return (int)std::lround(c * 255.0f);
}

static TextCoverRgb rgb8(int r, int g, int b) {
    return { r / 255.0f, g / 255.0f, b / 255.0f };
}

static void nearByte(TextCoverRgb c, int r, int g, int b) {
    assert(std::abs(byte8(c.r) - r) <= 1);
    assert(std::abs(byte8(c.g) - g) <= 1);
    assert(std::abs(byte8(c.b) - b) <= 1);
}

int main() {
    const TextCoverRgb rose  = rgb8(255, 240, 245);
    const TextCoverRgb navy  = rgb8(32, 35, 48);
    const TextCoverRgb pink  = rgb8(217, 64, 115);
    const TextCoverRgb paper = rgb8(245, 245, 240);
    const TextCoverRgb white = rgb8(242, 242, 242);
    const TextCoverRgb black = rgb8(13, 13, 13);

    // Full coverage is the text colour. Zero coverage is the page.
    nearByte(textCoverLinear(pink, paper, 1.0f), 217, 64, 115);
    nearByte(textCoverLinear(pink, paper, 0.0f), 245, 245, 240);
    nearByte(textCoverLegacy(pink, paper, 1.0f), 217, 64, 115);
    nearByte(textCoverLegacy(pink, paper, 0.0f), 245, 245, 240);

    // Rose on navy at half coverage. Legacy loses the hue (R == B). Linear
    // stays warm, and it is not the lighter fringe the gamma blend paints.
    const TextCoverRgb legRose = textCoverLegacy(rose, navy, 0.5f);
    const TextCoverRgb linRose = textCoverLinear(rose, navy, 0.5f);
    nearByte(legRose, 204, 194, 204);
    nearByte(linRose, 189, 178, 183);
    assert(byte8(legRose.r) == byte8(legRose.b));
    assert(byte8(linRose.r) > byte8(linRose.b));
    assert(linRose.g < legRose.g);

    // Saturated pink on a near-white page. Legacy clamps red to white.
    // Linear does not, and the edge is still pink.
    const TextCoverRgb legPink = textCoverLegacy(pink, paper, 0.25f);
    const TextCoverRgb linPink = textCoverLinear(pink, paper, 0.25f);
    nearByte(legPink, 255, 214, 238);
    nearByte(linPink, 238, 218, 217);
    assert(byte8(legPink.r) == 255);
    assert(byte8(linPink.r) < 250);
    assert(byte8(linPink.r) > byte8(linPink.b) + 10);

    // White on the dark theme at half coverage. Linear stays near 178.
    // Mixing the sRGB numbers themselves would land near 128 and look thin.
    const TextCoverRgb linWhite = textCoverLinear(white, black, 0.5f);
    const float gamma = white.r * 0.5f + black.r * 0.5f;
    assert(linWhite.r > 0.65f);
    assert(gamma < 0.55f);
    assert(linWhite.r > gamma + 0.15f);
    nearByte(linWhite, 178, 178, 178);

    return 0;
}
