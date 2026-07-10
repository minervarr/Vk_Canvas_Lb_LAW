#pragma once
#include "platform.hh"
#include "texture.hh"

class Renderer;

// Reads `path` via `assets` (the same platform.hh AssetReader seam
// MsdfFont::generate already uses for fonts), decodes it at a resolution
// sufficient for targetW x targetH via img_decode_kit, and uploads it as a
// texture. The general "load an image sized for where it'll actually be
// shown" primitive — any vk_canvas app wants this, not just one.
// outW/outH (optional) receive the actual decoded pixel dimensions (aspect
// ratio preserved), e.g. for a caller that wants to fit rather than stretch.
// Returns kInvalidTexture if the file can't be read or decoded.
// mips: forwarded to Renderer::create_texture. decodeImageScaled() lands
// within ~2x of targetW/H, so callers that draw at the target size can pass
// false (bilinear handles ≤2x minification fine; saves the mip chain's +33%
// VRAM and its per-upload blit pass). Keep true when the texture is drawn
// much smaller than the target it was decoded for.
TextureHandle createTextureFromImageFile(Renderer& renderer, AssetReader& assets,
                                         const char* path, int targetW, int targetH,
                                         int* outW = nullptr, int* outH = nullptr,
                                         bool mips = true);
