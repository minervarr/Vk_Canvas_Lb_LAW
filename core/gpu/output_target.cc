#include "output_target.hh"

#include <cmath>

namespace {

struct Candidate {
    OutputFormat     format;
    OutputColorSpace colorspace;
    OutputEncode    encode;
    bool            degraded;  // supported, but not what we'd choose first
};

// Preference tables from USAGE_hdr_output.md — first supported pair wins.
const Candidate kExtendedLinear[] = {
    {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::ExtendedSrgbLinear,
     OutputEncode::LinearScRgb, false},
    {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::Bt2020Linear,
     OutputEncode::LinearScRgb, false},
};

const Candidate kHdr10[] = {
    {OutputFormat::A2B10G10R10UnormPack32, OutputColorSpace::Hdr10St2084,
     OutputEncode::PQ, false},
    // Passthrough means the compositor does NOT interpret our colorspace; we
    // still write PQ and hope the panel is in PQ mode. Log loudly if used.
    {OutputFormat::A2B10G10R10UnormPack32, OutputColorSpace::PassThrough,
     OutputEncode::PQ, true},
};

OutputSelection sdrPin() {
    OutputSelection s{};
    s.format     = OutputFormat::R8G8B8A8Unorm;
    s.colorspace = OutputColorSpace::SrgbNonlinear;
    s.target     = OutputTarget::SdrSrgb;
    s.encode     = OutputEncode::Srgb;
    s.hdr        = false;
    return s;
}

bool supports(const SurfaceFormat* formats, size_t count,
              OutputFormat f, OutputColorSpace cs) {
    // The legacy "any format" reply is defined as a list of EXACTLY ONE entry
    // whose format is UNDEFINED: the surface has no preference and the caller
    // may pick freely. It never advertises an HDR colorspace, so only the SDR
    // pin matches. Scoped to count == 1 deliberately — a driver that emits a
    // stray UNDEFINED among real entries is non-conformant, and treating that
    // as the legacy reply would throw away every genuine pair after it.
    if (count == 1 && formats[0].format == OutputFormat::Undefined)
        return cs == OutputColorSpace::SrgbNonlinear;

    for (size_t i = 0; i < count; ++i)
        if (formats[i].format == f && formats[i].colorSpace == cs) return true;
    return false;
}

OutputSelection tryTable(const Candidate* table, size_t n,
                         const SurfaceFormat* formats, size_t count,
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

OutputSelection pickTarget(const SurfaceFormat* formats, size_t count,
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

float rolloffCurve(float x, float white, float ceiling) {
    constexpr float k = 0.8f;
    if (x <= k) return x;
    // The ceiling can never exceed the image's own white point. This curve
    // COMPRESSES -- it maps `white` down onto `ceiling` -- and handed a ceiling
    // above `white` it extrapolates instead, amplifying highlights and
    // overshooting the ceiling it was given. Clamping here makes the
    // already-fits case (ceiling >= white, the ordinary one on an HDR panel)
    // an exact identity, which is what "the panel can show this picture"
    // should mean. It also stops an SDR image being stretched across headroom
    // it never had.
    ceiling = std::fmin(ceiling, white);
    const float span = std::fmax(white - k, 1e-4f);
    const float a    = (x - k) / span;
    const float d    = span / std::fmax(ceiling - k, 1e-4f);
    return k + (ceiling - k) * (a * (1.0f + a / d) / (1.0f + a));
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
