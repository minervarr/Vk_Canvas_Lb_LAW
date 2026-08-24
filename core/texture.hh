#pragma once
#include <cstdint>

// Opaque handle to a GPU texture created via Renderer::create_texture().
// 0 is never a valid handle.
using TextureHandle = uint32_t;
constexpr TextureHandle kInvalidTexture = 0;

// Storage format of an uploaded texture.
//
// RGBA8_UNORM is what this engine has always used and stays the default, so
// every existing consumer is unaffected. The float formats exist for image
// data that carries MORE RANGE THAN THE DISPLAY CAN SHOW -- an HDR or
// high-bit-depth photograph, decoded to linear light and kept unclipped, so a
// viewer can move an exposure control through it without re-decoding. Values
// below 0 and above 1 are meaningful in those formats and must not be clamped
// on the way in; the clamp belongs at the very end, in the shader, after the
// tone curve.
enum class TextureFormat : uint8_t {
  RGBA8_UNORM = 0,   // 8 bits/channel, display-referred. The historical path.
  RGBA16F     = 1,   // half float, linear. ~0.05% relative error everywhere.
  RGBA32F     = 2,   // full float, linear. Loses nothing; costs 16 bytes/px.
};

inline uint32_t bytes_per_pixel(TextureFormat f) {
  switch (f) {
    case TextureFormat::RGBA32F: return 16u;
    case TextureFormat::RGBA16F: return 8u;
    default:                     return 4u;
  }
}

// How Canvas::image()/imageFg() draws are turned into display pixels.
//
// kPassthrough is the historical behaviour and the default: sample, premultiply,
// done -- correct for an already-display-referred RGBA8 texture, and bit-exact
// with what this engine did before tone mapping existed.
//
// The other two interpret the texture as LINEAR light and are for the float
// formats above. They differ only in what happens to values the display cannot
// reach, and that difference is the whole point: kClip tells the literal truth
// (anything over the top is white, and you find out by moving the exposure),
// while kRolloff compresses the highlights so an HDR photograph is legible on
// first sight. A viewer should be able to do both -- one to look, one to trust.
enum class ToneMode : uint8_t {
  kPassthrough = 0,
  kClip        = 1,
  kRolloff     = 2,
};

// One textured-quad draw emitted by Canvas::image(), consumed by ImageLayer.
// Drawn as a background layer, before the vector/text overlay, in screen
// pixels (not content-area-relative — Canvas::image() applies insets itself
// the same way rect()/text() do).
struct ImageDraw {
  TextureHandle tex = kInvalidTexture;
  float x = 0, y = 0, w = 0, h = 0;
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  bool  hasClip = false;
  float clipX = 0, clipY = 0, clipW = 0, clipH = 0;

  // Tone controls. Every default below reproduces the pre-tone-mapping
  // behaviour exactly, so an ImageDraw built the way any existing consumer
  // builds one renders identically to before.
  float exposure = 1.0f;   // linear gain, ALREADY exp2(EV) -- the shader does no pow
  float toneMode = 0.0f;   // a ToneMode, as a float because push constants are floats
  float white    = 1.0f;   // kRolloff knee's white point, in linear units, >= 1
  float clipWarn = 0.0f;   // non-zero: stripe pixels that exceed the display range
};
