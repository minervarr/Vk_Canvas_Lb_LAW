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

// Decodes an in-memory image file to RGBA8, sized for targetW x targetH —
// JPEG via turbojpeg's real scaled decompression (decodes fewer pixels in
// the first place, critical for very large sources); everything else via
// stb_image, downsampled with a box filter after decode (stb_image has no
// scaled-decode API). Never more resolution than
// sufficientImageResolution() wants, never more than the source has.
DecodedImage decodeImageScaled(const uint8_t* fileBytes, size_t size, int targetW, int targetH);
