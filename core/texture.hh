#pragma once
#include <cstdint>

// Opaque handle to a GPU texture created via Renderer::create_texture().
// 0 is never a valid handle.
using TextureHandle = uint32_t;
constexpr TextureHandle kInvalidTexture = 0;

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
};
