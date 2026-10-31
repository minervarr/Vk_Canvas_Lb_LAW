#pragma once
#include "platform.hh"
#include "texture.hh"
#include "img_decode.hh"   // ImageFit (img_decode_kit is a PUBLIC dep of this lib)

class Renderer;

// Reads `path` via `assets` (the same platform.hh AssetReader seam
// MsdfFont::generate already uses for fonts), decodes it at a resolution
// sufficient for targetW x targetH via img_decode_kit, and uploads it as a
// texture. The general "load an image sized for where it'll actually be
// shown" primitive — any vk_canvas app wants this, not just one.
// outW/outH (optional) receive the actual decoded pixel dimensions (aspect
// ratio preserved), e.g. for a caller that wants to fit rather than stretch.
// Returns kInvalidTexture if the file can't be read or decoded.
// fit: how targetW/H relate to the pixels the image will occupy — see
// ImageFit. Pass kContain when the caller aspect-fits the result into that
// box (e.g. a fullscreen viewer), so the decode lands at the drawn size
// instead of the box's larger axis.
// mips: forwarded to Renderer::create_texture. A mip chain is what keeps a
// texture drawn MUCH smaller than its decode size from aliasing — but the
// sampler picks its LOD from the on-screen ratio, so it also softens anything
// drawn even slightly below 1:1. Pass false when decode size ≈ draw size
// (kContain gets you there, as does decoding a grid tile at tile size): level
// 0 is then the only level worth sampling, and skipping the chain saves +33%
// VRAM and a per-upload blit pass. Keep true only when the same texture is
// deliberately shown much smaller than it was decoded for.
TextureHandle createTextureFromImageFile(Renderer& renderer, AssetReader& assets,
                                         const char* path, int targetW, int targetH,
                                         int* outW = nullptr, int* outH = nullptr,
                                         bool mips = true,
                                         ImageFit fit = ImageFit::kPerAxis);
