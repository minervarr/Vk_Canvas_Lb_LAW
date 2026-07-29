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
#include <thread>

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

// Resolves the target box + fit mode into the wanted decode size for a source
// of nativeW x nativeH. kPerAxis sizes each axis against its own budget;
// kContain first works out how big the image will really be drawn (see
// ImageFit) and asks for exactly that. Both clamp to the source resolution.
static void containDrawnSize(int nativeW, int nativeH, int targetW, int targetH,
                             int* drawnW, int* drawnH) {
    const double s = std::min((double)targetW / nativeW, (double)targetH / nativeH);
    *drawnW = std::max(1, (int)std::lround(nativeW * s));
    *drawnH = std::max(1, (int)std::lround(nativeH * s));
}

static void wantedDecodeSize(int nativeW, int nativeH, int targetW, int targetH,
                             ImageFit fit, int* wantW, int* wantH) {
    if (fit == ImageFit::kContain && nativeW > 0 && nativeH > 0) {
        int drawnW = 0, drawnH = 0;
        containDrawnSize(nativeW, nativeH, targetW, targetH, &drawnW, &drawnH);
        // Clamped to native: the DECODE should never ask for more pixels than
        // the file holds. Enlarging past that (when the image will be shown
        // bigger than it is) is the resampler's job, not the decoder's.
        *wantW = sufficientImageResolution(nativeW, drawnW, /*qualityMargin=*/1.0f);
        *wantH = sufficientImageResolution(nativeH, drawnH, /*qualityMargin=*/1.0f);
        return;
    }
    *wantW = sufficientImageResolution(nativeW, targetW);
    *wantH = sufficientImageResolution(nativeH, targetH);
}

// ── Magic Kernel Sharp 2013 resampler (see resampleRGBA in the header) ──────
namespace {

// The kernel itself. Support is |x| <= 2.5; the middle and outer pieces go
// negative, which is what sharpens — a purely positive kernel (box, tent/
// bilinear, Gaussian) can only ever blur.
inline double mks2013(double x) {
    x = std::fabs(x);
    if (x <= 0.5) return 17.0 / 16.0 - 7.0 / 4.0 * x * x;
    if (x <= 1.5) return x * x - 11.0 / 4.0 * x + 7.0 / 4.0;
    if (x <= 2.5) return -1.0 / 8.0 * x * x + 5.0 / 8.0 * x - 25.0 / 32.0;
    return 0.0;
}

// One output sample's tap run: [first, first+n) in source space (may hang off
// either edge — reads clamp), weights at weights[index..index+n).
struct Taps {
    int    first = 0;
    int    n     = 0;
    size_t index = 0;
};

// Builds the per-output taps for a 1D nin -> nout resample. Downscaling
// stretches the kernel in source space (support 2.5/scale) so every source
// pixel still contributes — that's what makes this an area-correct filter and
// not point sampling with extra steps.
void buildKernel(int nin, int nout, std::vector<Taps>& taps,
                 std::vector<float>& weights) {
    const double scale   = (double)nout / (double)nin;
    const double support = 2.5 / std::min(scale, 1.0);
    taps.assign(nout, Taps{});
    weights.clear();
    weights.reserve((size_t)nout * (size_t)(2.0 * support + 2.0));

    std::vector<double> row;
    for (int o = 0; o < nout; o++) {
        // +0.5/-0.5 puts the center on the pixel's center, not its edge.
        const double c     = (o + 0.5) / scale - 0.5;
        const int    first = (int)std::floor(c - support);
        const int    last  = (int)std::ceil(c + support);
        const int    n     = last - first + 1;

        row.assign(n, 0.0);
        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            const double d = (double)(first + i) - c;
            const double w = mks2013(scale < 1.0 ? d * scale : d);
            row[i] = w;
            sum += w;
        }
        // Normalize to unit gain, so flat areas keep their exact value and the
        // negative lobes can't drift the overall brightness.
        const size_t base = weights.size();
        weights.resize(base + n);
        for (int i = 0; i < n; i++)
            weights[base + i] = (float)(sum != 0.0 ? row[i] / sum : 0.0);

        taps[o] = Taps{first, n, base};
    }
}

// Runs body(begin, end) over [0, n) split across the hardware's cores. A
// resample of a fullscreen image is tens of milliseconds single-threaded —
// enough to be felt as a hitch on the thread that also draws — and both
// passes are embarrassingly parallel by row, with disjoint writes. Falls back
// to running inline when there's one core or too little work to pay for the
// thread launch.
template <typename Body>
void parallelRows(int n, const Body& body) {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 1;
    const int chunks = (int)std::min<unsigned>(hw, (unsigned)std::max(1, n / 64));
    if (chunks <= 1) { body(0, n); return; }

    std::vector<std::thread> pool;
    pool.reserve(chunks - 1);
    const int per = (n + chunks - 1) / chunks;
    for (int c = 1; c < chunks; c++) {
        const int b = c * per, e = std::min(n, b + per);
        if (b >= e) break;
        pool.emplace_back([&body, b, e] { body(b, e); });
    }
    body(0, std::min(n, per));      // this thread takes the first chunk
    for (auto& t : pool) t.join();
}

inline uint8_t clamp255(float v) {
    // Negative lobes overshoot at hard edges (that IS the sharpening); clip
    // rather than rescale, exactly like every other integer implementation.
    const int i = (int)std::lround(v);
    return (uint8_t)(i < 0 ? 0 : (i > 255 ? 255 : i));
}

}  // namespace

void resampleRGBA(const uint8_t* src, int srcW, int srcH,
                  uint8_t* dst, int dstW, int dstH) {
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;

    std::vector<Taps>  tapsX, tapsY;
    std::vector<float> wX, wY;
    buildKernel(srcW, dstW, tapsX, wX);
    buildKernel(srcH, dstH, tapsY, wY);

    // Horizontal pass into a float scratch (dstW x srcH). Keeping the
    // intermediate in float matters: rounding to 8-bit between the two passes
    // would quantize away part of what the sharpening just recovered.
    std::vector<float> tmp((size_t)dstW * (size_t)srcH * 4);
    parallelRows(srcH, [&](int y0, int y1) {
        for (int y = y0; y < y1; y++) {
            const uint8_t* srow = src + (size_t)y * srcW * 4;
            float*         trow = tmp.data() + (size_t)y * dstW * 4;
            for (int x = 0; x < dstW; x++) {
                const Taps& t = tapsX[x];
                float acc[4] = {0, 0, 0, 0};
                for (int i = 0; i < t.n; i++) {
                    int sx = t.first + i;
                    sx = sx < 0 ? 0 : (sx >= srcW ? srcW - 1 : sx);   // clamp to edge
                    const float    w = wX[t.index + i];
                    const uint8_t* p = srow + (size_t)sx * 4;
                    acc[0] += w * p[0]; acc[1] += w * p[1];
                    acc[2] += w * p[2]; acc[3] += w * p[3];
                }
                float* o = trow + (size_t)x * 4;
                o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2]; o[3] = acc[3];
            }
        }
    });

    // Vertical pass, accumulating whole rows at a time so the strided reads
    // stay sequential (the naive per-pixel loop thrashes cache on tall images).
    parallelRows(dstH, [&](int y0, int y1) {
        std::vector<float> acc((size_t)dstW * 4);
        for (int y = y0; y < y1; y++) {
            const Taps& t = tapsY[y];
            std::fill(acc.begin(), acc.end(), 0.0f);
            for (int i = 0; i < t.n; i++) {
                int sy = t.first + i;
                sy = sy < 0 ? 0 : (sy >= srcH ? srcH - 1 : sy);       // clamp to edge
                const float  w    = wY[t.index + i];
                const float* trow = tmp.data() + (size_t)sy * dstW * 4;
                for (size_t k = 0; k < acc.size(); k++) acc[k] += w * trow[k];
            }
            uint8_t* drow = dst + (size_t)y * dstW * 4;
            for (size_t k = 0; k < acc.size(); k++) drow[k] = clamp255(acc[k]);
        }
    });
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
// most of it away afterward. Decompresses straight into `out` (sized here):
// libjpeg writes wherever it's pointed, so a tjAlloc'd scratch buffer plus a
// full-image memcpy into the result would be pure waste.
static bool decodeJpegRGBA(const uint8_t* data, size_t size, int targetW, int targetH,
                           ImageFit fit, std::vector<uint8_t>& out, int* outW, int* outH) {
    tjhandle tj = tjInitDecompress();
    if (!tj) return false;

    int jpegW = 0, jpegH = 0, subsamp = 0, colorspace = 0;
    if (tjDecompressHeader3(tj, data, (unsigned long)size, &jpegW, &jpegH, &subsamp, &colorspace) != 0) {
        tjDestroy(tj);
        return false;
    }

    int wantW = 0, wantH = 0;
    wantedDecodeSize(jpegW, jpegH, targetW, targetH, fit, &wantW, &wantH);

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

    out.resize((size_t)bestW * (size_t)bestH * 4);

    // No TJFLAG_FASTUPSAMPLE: cover art is essentially always 4:2:0, and the
    // fast chroma upsampler (pixel replication) visibly coarsens color detail
    // at edges versus libjpeg's default fancy (triangle) upsampling — the
    // difference every other image viewer shows, since none of them opt into
    // the fast path. Costs a few ms on a decode that happens once per image.
    // The DCT stays libjpeg-turbo's accurate default (no TJFLAG_FASTDCT).
    if (tjDecompress2(tj, data, (unsigned long)size, out.data(), bestW, /*pitch=*/0, bestH,
                       TJPF_RGBA, /*flags=*/0) != 0) {
        out.clear();
        tjDestroy(tj);
        return false;
    }
    tjDestroy(tj);
    *outW = bestW; *outH = bestH;
    return true;
}
#endif  // IMG_DECODE_HAVE_TURBOJPEG

DecodedImage decodeImageScaled(const uint8_t* fileBytes, size_t size,
                               int targetW, int targetH, ImageFit fit) {
    DecodedImage out;
    int w = 0, h = 0;

#if IMG_DECODE_HAVE_TURBOJPEG
    if (isJpeg(fileBytes, size))
        decodeJpegRGBA(fileBytes, size, targetW, targetH, fit, out.rgba, &w, &h);
#endif

    if (out.rgba.empty()) {
        int channels = 0;
        uint8_t* stb = stbi_load_from_memory(fileBytes, (int)size, &w, &h, &channels, 4);
        if (!stb) return out;   // empty rgba signals failure
        int wantW = 0, wantH = 0;
        wantedDecodeSize(w, h, targetW, targetH, fit, &wantW, &wantH);
        if (fit != ImageFit::kContain && (wantW < w || wantH < h)) {
            // No scaled-decode API in stb_image (unlike turbojpeg): decode
            // full-res, then a single box-filter pass down to size. kContain
            // skips this — the exact-size resample below supersedes it, and
            // box-then-resample would blur once for nothing.
            out.rgba = boxDownsampleRGBA(stb, w, h, wantW, wantH);
            w = wantW; h = wantH;
        } else {
            out.rgba.assign(stb, stb + (size_t)w * h * 4);
        }
        stbi_image_free(stb);
    }
    out.w = w; out.h = h;

    // kContain promises the EXACT drawn size so the caller can blit 1:1.
    // Scaled decode only lands on its own fixed steps (turbojpeg's eighths),
    // and stb_image has no scaled decode at all, so finish with a real
    // resample. Skipped when the decode already landed exactly — the common
    // case for an image whose native size is at or below the target.
    if (fit == ImageFit::kContain) {
        // Recomputed from the DECODED size, not the native one — same aspect,
        // same box, so the same answer, and it stays right whichever decode
        // path ran. Not clamped to native here: an image shown larger than it
        // is gets enlarged by this kernel rather than by the GPU's bilinear.
        int exactW = 0, exactH = 0;
        containDrawnSize(w, h, targetW, targetH, &exactW, &exactH);
        if (exactW != w || exactH != h) {
            std::vector<uint8_t> resized((size_t)exactW * (size_t)exactH * 4);
            resampleRGBA(out.rgba.data(), w, h, resized.data(), exactW, exactH);
            out.rgba = std::move(resized);
            out.w = exactW; out.h = exactH;
        }
    }
    return out;
}
