#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Smallest source resolution that loses no visible quality when displayed at
// targetPx, with headroom for imperfect scaling/aspect-fit — never larger
// than nativePx (no invented detail from upscale-decoding). Cover art/photo
// files in the wild can be far larger than any on-screen use needs (e.g.
// 30000px sources for a ~150px thumbnail); decoding/uploading at native
// resolution wastes memory and CPU decode time for zero visible benefit,
// since anything beyond the on-screen size is thrown away by
// downsampling/mipmapping.
int sufficientImageResolution(int nativePx, int targetPx, float qualityMargin = 1.5f);

struct DecodedImage {
    std::vector<uint8_t> rgba;  // RGBA8, straight alpha, row-major. Empty on failure.
    int w = 0, h = 0;
};

// Resamples RGBA8 to exactly dstW x dstH using Magic Kernel Sharp 2013
// (John Costella's kernel; the same one swayimg and Facebook's image pipeline
// use). Separable, 2.5-pixel support, with the negative side lobes that make
// it re-sharpen as it scales — unlike a box/bilinear filter, which only ever
// averages and therefore always lands softer than the source.
//
// This exists because GPU sampling is NOT a substitute. A texture drawn at any
// ratio other than 1:1 gets VK_FILTER_LINEAR's 2x2 bilinear tap (plus mip
// blending, if the texture has a chain), and no decode-size tuning recovers
// the detail that costs. Resampling once here, to the exact size the image
// will occupy, lets the draw be a 1:1 blit where the sampler cannot soften
// anything — and it's cheaper per frame, since the GPU stops filtering a
// larger-than-needed texture on every redraw.
//
// src and dst must not overlap. Operates on the values as stored (gamma
// space), matching every other viewer that does this on 8-bit data.
void resampleRGBA(const uint8_t* src, int srcW, int srcH,
                  uint8_t* dst, int dstW, int dstH);

// How targetW x targetH relates to the pixels the image will actually occupy.
enum class ImageFit {
    // The target is a per-axis resolution budget: size each axis independently
    // against it (with sufficientImageResolution's quality margin). Right when
    // the caller stretches to the target, or doesn't know the final geometry.
    kPerAxis,
    // The image will be drawn aspect-fit INSIDE the target box, so its real
    // on-screen size is native * min(targetW/nativeW, targetH/nativeH) — often
    // far less than the box on the non-binding axis (a square cover in a 16:9
    // window shows at the window's HEIGHT, not its width). This mode returns
    // the image at EXACTLY that drawn size — scaled decode gets as close as
    // its fixed steps allow, then resampleRGBA() finishes the job — so the
    // caller can blit it 1:1 and never let a GPU sampler scale it. That is
    // the whole point: bilinear minification, with or without mips, is what
    // makes a scaled-on-the-GPU image read softer than the same file in a
    // dedicated viewer. Never upscales past the source resolution.
    kContain,
};

// Decodes an in-memory image file to RGBA8, sized for targetW x targetH —
// JPEG via turbojpeg's real scaled decompression (decodes fewer pixels in
// the first place, critical for very large sources); everything else via
// stb_image, downsampled with a box filter after decode (stb_image has no
// scaled-decode API). Never more resolution than the chosen ImageFit wants,
// never more than the source has.
DecodedImage decodeImageScaled(const uint8_t* fileBytes, size_t size,
                               int targetW, int targetH,
                               ImageFit fit = ImageFit::kPerAxis);
