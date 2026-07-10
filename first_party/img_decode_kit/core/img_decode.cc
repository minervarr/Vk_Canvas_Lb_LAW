#include "img_decode.hh"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#if IMG_DECODE_HAVE_TURBOJPEG
#include <turbojpeg.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

// Cover art/photos are overwhelmingly JPEG; stb_image's baseline-only JPEG
// decoder (no progressive support, simpler/lower-quality chroma upsampling)
// is a visible step down from libjpeg-turbo. Decode JPEGs via turbojpeg and
// fall back to stb_image for everything else (PNG, etc) — and for JPEG too
// when the consuming build didn't provide a turbojpeg-static target (see
// CMakeLists.txt: this library works standalone either way, since stb_image
// alone can still decode every format, just without turbojpeg's speed/
// quality edge on the JPEG path specifically).
#if IMG_DECODE_HAVE_TURBOJPEG
static bool isJpeg(const uint8_t* data, size_t size) {
    return size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}
#endif

int sufficientImageResolution(int nativePx, int targetPx, float qualityMargin) {
    int wanted = (int)std::lround(targetPx * qualityMargin);
    return std::min(nativePx, std::max(1, wanted));
}

// Box-filter downsample (simple area average), used for the stb_image path
// (PNG/etc — turbojpeg's DCT scaling handles the JPEG case instead, decoding
// fewer pixels in the first place rather than decoding-then-discarding).
static std::vector<uint8_t> boxDownsampleRGBA(const uint8_t* src, int srcW, int srcH,
                                              int dstW, int dstH) {
    std::vector<uint8_t> dst((size_t)dstW * dstH * 4);
    for (int y = 0; y < dstH; y++) {
        int y0 = (int)((int64_t)y * srcH / dstH), y1 = (int)((int64_t)(y + 1) * srcH / dstH);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dstW; x++) {
            int x0 = (int)((int64_t)x * srcW / dstW), x1 = (int)((int64_t)(x + 1) * srcW / dstW);
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t sum[4] = {0, 0, 0, 0};
            int count = 0;
            for (int sy = y0; sy < y1 && sy < srcH; sy++) {
                for (int sx = x0; sx < x1 && sx < srcW; sx++) {
                    const uint8_t* p = src + ((size_t)sy * srcW + sx) * 4;
                    sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3];
                    count++;
                }
            }
            uint8_t* d = dst.data() + ((size_t)y * dstW + x) * 4;
            if (count == 0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
            d[0] = (uint8_t)(sum[0] / count); d[1] = (uint8_t)(sum[1] / count);
            d[2] = (uint8_t)(sum[2] / count); d[3] = (uint8_t)(sum[3] / count);
        }
    }
    return dst;
}

#if IMG_DECODE_HAVE_TURBOJPEG
// Decodes at (at most) the smallest DCT scaling factor turbojpeg offers that
// still covers sufficientImageResolution(nativeDim, targetDim) on the larger
// side — avoids decoding a 30000px source at full resolution just to throw
// most of it away afterward. Returns a tjAlloc'd buffer (caller must tjFree).
static uint8_t* decodeJpegRGBA(const uint8_t* data, size_t size, int targetW, int targetH,
                               int* outW, int* outH) {
    tjhandle tj = tjInitDecompress();
    if (!tj) return nullptr;

    int jpegW = 0, jpegH = 0, subsamp = 0, colorspace = 0;
    if (tjDecompressHeader3(tj, data, (unsigned long)size, &jpegW, &jpegH, &subsamp, &colorspace) != 0) {
        tjDestroy(tj);
        return nullptr;
    }

    int wantW = sufficientImageResolution(jpegW, targetW);
    int wantH = sufficientImageResolution(jpegH, targetH);

    int numFactors = 0;
    tjscalingfactor* factors = tjGetScalingFactors(&numFactors);
    int bestW = jpegW, bestH = jpegH;
    if (factors) {
        // Factors are listed largest-scale-first; keep the smallest (last
        // scanned) whose output still covers both wanted dimensions.
        for (int i = 0; i < numFactors; i++) {
            int sw = TJSCALED(jpegW, factors[i]);
            int sh = TJSCALED(jpegH, factors[i]);
            if (sw >= wantW && sh >= wantH) { bestW = sw; bestH = sh; }
        }
    }

    uint8_t* rgba = (uint8_t*)tjAlloc(bestW * bestH * 4);
    if (!rgba) { tjDestroy(tj); return nullptr; }

    if (tjDecompress2(tj, data, (unsigned long)size, rgba, bestW, /*pitch=*/0, bestH,
                       TJPF_RGBA, TJFLAG_FASTUPSAMPLE) != 0) {
        tjFree(rgba);
        tjDestroy(tj);
        return nullptr;
    }
    tjDestroy(tj);
    *outW = bestW; *outH = bestH;
    return rgba;
}
#endif  // IMG_DECODE_HAVE_TURBOJPEG

DecodedImage decodeImageScaled(const uint8_t* fileBytes, size_t size, int targetW, int targetH) {
    DecodedImage out;
    int w = 0, h = 0;
    uint8_t* rgba = nullptr;
    enum class Owner { kNone, kTurbojpeg, kStbImage } owner = Owner::kNone;

#if IMG_DECODE_HAVE_TURBOJPEG
    if (isJpeg(fileBytes, size)) {
        rgba = decodeJpegRGBA(fileBytes, size, targetW, targetH, &w, &h);
        if (rgba) owner = Owner::kTurbojpeg;
    }
#endif

    std::vector<uint8_t> downsampled;  // holds the buffer if stb_image's decode needs resizing
    if (!rgba) {
        int channels = 0;
        rgba = stbi_load_from_memory(fileBytes, (int)size, &w, &h, &channels, 4);
        if (rgba) {
            owner = Owner::kStbImage;
            int wantW = sufficientImageResolution(w, targetW);
            int wantH = sufficientImageResolution(h, targetH);
            if (wantW < w || wantH < h) {
                // No scaled-decode API in stb_image (unlike turbojpeg): decode
                // full-res, then a single box-filter pass down to size.
                downsampled = boxDownsampleRGBA(rgba, w, h, wantW, wantH);
                stbi_image_free(rgba);
                rgba = downsampled.data();
                w = wantW; h = wantH;
                owner = Owner::kNone;  // already freed; downsampled vector owns it now
            }
        }
    }
    if (!rgba) return out;  // empty rgba signals failure

    out.rgba.resize((size_t)w * h * 4);
    std::memcpy(out.rgba.data(), rgba, out.rgba.size());
    out.w = w; out.h = h;

#if IMG_DECODE_HAVE_TURBOJPEG
    if (owner == Owner::kTurbojpeg) tjFree(rgba);
#endif
    if (owner == Owner::kStbImage) stbi_image_free(rgba);
    return out;
}
