#include "output_target.hh"

#include <cmath>

namespace {

struct Candidate {
    VkFormat        format;
    VkColorSpaceKHR colorspace;
    OutputEncode    encode;
    bool            degraded;  // supported, but not what we'd choose first
};

// Preference tables from USAGE_hdr_output.md — first supported pair wins.
const Candidate kExtendedLinear[] = {
    {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
     OutputEncode::LinearScRgb, false},
    {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_BT2020_LINEAR_EXT,
     OutputEncode::LinearScRgb, false},
};

const Candidate kHdr10[] = {
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT,
     OutputEncode::PQ, false},
    // Passthrough means the compositor does NOT interpret our colorspace; we
    // still write PQ and hope the panel is in PQ mode. Log loudly if used.
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_PASS_THROUGH_EXT,
     OutputEncode::PQ, true},
};

OutputSelection sdrPin() {
    OutputSelection s{};
    s.format     = VK_FORMAT_R8G8B8A8_UNORM;
    s.colorspace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    s.target     = OutputTarget::SdrSrgb;
    s.encode     = OutputEncode::Srgb;
    s.hdr        = false;
    return s;
}

bool supports(const VkSurfaceFormatKHR* formats, size_t count,
              VkFormat f, VkColorSpaceKHR cs) {
    // The legacy "any format" reply is defined as a list of EXACTLY ONE entry
    // whose format is UNDEFINED: the surface has no preference and the caller
    // may pick freely. It never advertises an HDR colorspace, so only the SDR
    // pin matches. Scoped to count == 1 deliberately — a driver that emits a
    // stray UNDEFINED among real entries is non-conformant, and treating that
    // as the legacy reply would throw away every genuine pair after it.
    if (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
        return cs == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    for (size_t i = 0; i < count; ++i)
        if (formats[i].format == f && formats[i].colorSpace == cs) return true;
    return false;
}

OutputSelection tryTable(const Candidate* table, size_t n,
                         const VkSurfaceFormatKHR* formats, size_t count,
                         OutputTarget requested) {
    for (size_t i = 0; i < n; ++i) {
        if (!supports(formats, count, table[i].format, table[i].colorspace))
            continue;
        OutputSelection s{};
        s.format     = table[i].format;
        s.colorspace = table[i].colorspace;
        s.target     = requested;
        s.encode     = table[i].encode;
        s.hdr        = true;
        s.degraded   = table[i].degraded;
        return s;
    }
    OutputSelection s = sdrPin();
    s.fellBack = true;
    return s;
}

}  // namespace

OutputSelection pickTarget(const VkSurfaceFormatKHR* formats, size_t count,
                           bool extColorspace, OutputTarget requested) {
    if (requested == OutputTarget::SdrSrgb) return sdrPin();

    // Every HDR colorspace enum below is defined by VK_EXT_swapchain_colorspace.
    // Without the instance extension, passing one to vkCreateSwapchainKHR is
    // invalid usage no matter what the (then equally invalid) list says.
    if (!extColorspace || count == 0) {
        OutputSelection s = sdrPin();
        s.fellBack = true;
        return s;
    }

    switch (requested) {
        case OutputTarget::ExtendedLinearScrgb:
            return tryTable(kExtendedLinear,
                            sizeof(kExtendedLinear) / sizeof(kExtendedLinear[0]),
                            formats, count, requested);
        case OutputTarget::Hdr10PQ:
            return tryTable(kHdr10, sizeof(kHdr10) / sizeof(kHdr10[0]),
                            formats, count, requested);
        case OutputTarget::SdrSrgb:
            break;
    }
    return sdrPin();
}

float pqEncode(float y) {
    // SMPTE ST 2084 constants, exact rationals rather than rounded decimals.
    constexpr float m1 = 0.1593017578125f;  // 2610 / 16384
    constexpr float m2 = 78.84375f;         // 2523 / 4096 * 128
    constexpr float c1 = 0.8359375f;        // 3424 / 4096
    constexpr float c2 = 18.8515625f;       // 2413 / 4096 * 32
    constexpr float c3 = 18.6875f;          // 2392 / 4096 * 32
    const float yp = std::pow(y > 0.0f ? y : 0.0f, m1);
    return std::pow((c1 + c2 * yp) / (1.0f + c3 * yp), m2);
}

const char* outputTargetName(OutputTarget t) {
    switch (t) {
        case OutputTarget::SdrSrgb:             return "SdrSrgb";
        case OutputTarget::ExtendedLinearScrgb: return "ExtendedLinearScrgb";
        case OutputTarget::Hdr10PQ:             return "Hdr10PQ";
    }
    return "?";
}

const char* outputEncodeName(OutputEncode e) {
    switch (e) {
        case OutputEncode::Srgb:        return "Srgb";
        case OutputEncode::LinearScRgb: return "LinearScRgb";
        case OutputEncode::PQ:          return "PQ";
    }
    return "?";
}
