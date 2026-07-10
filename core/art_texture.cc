#include "art_texture.hh"
#include "renderer.hh"
#include "img_decode.hh"

TextureHandle createTextureFromImageFile(Renderer& renderer, AssetReader& assets,
                                         const char* path, int targetW, int targetH,
                                         int* outW, int* outH, bool mips) {
    std::vector<uint8_t> fileBytes;
    if (!assets.read(path, fileBytes) || fileBytes.empty()) return kInvalidTexture;

    DecodedImage img = decodeImageScaled(fileBytes.data(), fileBytes.size(), targetW, targetH);
    if (img.rgba.empty()) return kInvalidTexture;

    TextureHandle tex = renderer.create_texture(img.rgba.data(), (uint32_t)img.w, (uint32_t)img.h,
                                                mips);
    if (outW) *outW = img.w;
    if (outH) *outH = img.h;
    return tex;
}
