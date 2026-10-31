#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Swapchain output target selection — pure logic, no Vulkan calls.
//
// What surface we present into, decided once at swapchain creation from the
// consumer's request and what the surface actually enumerates. Deliberately
// separate from OUTPUT_ENCODE (how pixels are written into that surface,
// baked per pipeline as a specialization constant) — see USAGE_hdr_output.md.
//
// HDR is always *requested* by the consumer, never guessed from content, and
// every request degrades to the SDR pin when the device doesn't advertise a
// matching pair. SdrSrgb resolves bit-identically to the pre-HDR behavior.

enum class OutputTarget {
    SdrSrgb = 0,          // R8G8B8A8_UNORM + SRGB_NONLINEAR (the default)
    ExtendedLinearScrgb,  // FP16 linear, >1.0 legal, 1.0 == SDR reference white
    Hdr10PQ,              // 10-bit ST 2084 PQ
};

// Which present mode the swapchain should prefer.
//
// Two consumers want opposite things and the engine had only ever served one.
//
// A camera preview wants Latency: MAILBOX, where the newest rendered frame
// replaces whatever was queued and the producer is never blocked. That was
// chosen against a real failure — Android's compositor periodically held an
// image for ~60 ms, which with a tight image count starved rendering and
// froze the preview to about 1 Hz.
//
// A video player wants Vsync: FIFO, and for the same reason in reverse. Its
// frames are not "the newest thing available", they are scheduled for
// specific instants by a clock; there is no newer frame to prefer, and
// nothing to gain from never blocking. What MAILBOX costs it is real: with no
// backpressure the render loop free-runs — measured at about 900 fps,
// redrawing identical content — and the heat that makes is the hardware
// decoder's problem too. FIFO makes vkQueuePresentKHR the pacer, one frame
// per vsync, which is what a player wants a present call to do.
enum class PresentPolicy {
    Latency = 0,  // prefer MAILBOX, fall back to FIFO. The default; unchanged.
    Vsync,        // FIFO always. Paced by the display, and no wasted frames.
};

// How a fragment stage must encode its output for the resolved target. Mirrors
// the OUTPUT_ENCODE specialization constant consumed by the shaders.
enum class OutputEncode {
    Srgb = 0,       // encode + dither + saturate (current behavior, byte-exact)
    LinearScRgb = 1,// linearize authored UI colors; image path skips saturate
    PQ = 2,         // tone-map to target range, then ST 2084 OETF
};

// The engine's own format/colorspace description. Deliberately Vulkan-free:
// the picker is pure logic and is tested without a device. The renderer
// converts to Vulkan at the swapchain boundary (see the compatibility block at
// the bottom, available when a translation unit has already included vulkan.h).
enum class OutputFormat : uint32_t {
    Undefined = 0,
    R8G8B8A8Unorm,
    B8G8R8A8Unorm,
    R16G16B16A16Sfloat,
    A2B10G10R10UnormPack32,
};

enum class OutputColorSpace : uint32_t {
    SrgbNonlinear = 0,
    ExtendedSrgbLinear,
    Bt2020Linear,
    Hdr10St2084,
    PassThrough,
};

struct SurfaceFormat {
    OutputFormat      format     = OutputFormat::Undefined;
    OutputColorSpace  colorSpace = OutputColorSpace::SrgbNonlinear;
};

struct OutputSelection {
    OutputFormat     format     = OutputFormat::R8G8B8A8Unorm;
    OutputColorSpace colorspace = OutputColorSpace::SrgbNonlinear;
    OutputTarget    target     = OutputTarget::SdrSrgb;
    OutputEncode    encode     = OutputEncode::Srgb;
    bool            hdr        = false;
    // True when the request could not be honored and we fell back to the SDR
    // pin — the caller logs this loudly.
    bool            fellBack   = false;
    // True when a non-preferred candidate was used (today: the PQ passthrough
    // 10-bit pair, whose colorspace the compositor does not interpret). Also
    // worth a loud log.
    bool            degraded   = false;
};

// Resolve `requested` against one surface format enumeration. `extColorspace`
// is whether VK_EXT_swapchain_colorspace was enabled at instance level —
// without it, only SRGB_NONLINEAR is meaningful, so every HDR request falls
// back regardless of what the list claims.
//
// A single {Undefined, ...} entry (the legacy "any format" reply) is treated
// as "the SDR pin is available", nothing more.
OutputSelection pickTarget(const SurfaceFormat* formats, size_t count,
                           bool extColorspace, OutputTarget requested);

inline OutputSelection pickTarget(const std::vector<SurfaceFormat>& formats,
                                  bool extColorspace, OutputTarget requested) {
    return pickTarget(formats.data(), formats.size(), extColorspace, requested);
}

// ── ST 2084 (PQ) OETF ───────────────────────────────────────────────────────
//
// THIS IS THE AUTHORITY for the PQ constants. shaders_src/output_encode.slang
// mirrors this function, and the shader cannot be unit-tested (it needs slangc
// plus a GPU), so the golden values in core/tests/output_target_test.cc guard
// the numbers on this side and the shader is kept identical by inspection.
// Change one, change both.
//
// `y` is absolute luminance normalized so 1.0 == 10000 nits. Returns the
// non-linear code value in [0,1].
float pqEncode(float y);

// ── The highlight rolloff curve ─────────────────────────────────────────────
//
// THIS IS THE AUTHORITY for the tone curve, on the same terms as pqEncode()
// above: shaders_src/image_frag.slang mirrors it, the shader cannot be
// unit-tested, so core/tests/output_target_test.cc guards the maths here.
//
// Extended Reinhard with a linear knee at k = 0.8. Below k it is the IDENTITY,
// so an ordinary SDR image passes through numerically untouched. Above k the
// range is compressed monotonically toward `ceiling`.
//
// `ceiling` is what the highlights roll TOWARD -- 1.0 (display white) on an
// SDR target, the display's headroom on an HDR one. That parameter is the
// whole reason this is not hardcoded: pinned at 1.0, the rolloff path can
// never emit a value above display white, so an HDR swapchain could be
// requested, granted, and then handed nothing but SDR-range pixels. At
// ceiling == 1.0 this is bit-identical to the pre-HDR curve.
//
// The curve is UNBOUNDED -- it crosses `ceiling` and keeps going; capping is
// the caller's saturate/desaturate step, exactly as before.
float rolloffCurve(float x, float white, float ceiling);

// Absolute luminance that PQ code value 1.0 represents.
inline constexpr float kPQPeakNits = 10000.0f;
// BT.2408 "graphics white": UI authored as white should land here, NOT at the
// panel's peak — rendering menus at 1000 nits is how HDR UI becomes painful.
inline constexpr float kGraphicsWhiteNits = 203.0f;

const char* outputTargetName(OutputTarget t);
const char* outputEncodeName(OutputEncode e);

// Binds the OUTPUT_ENCODE specialization constant (constant_id 0, an int) that
// shaders_src/output_encode.slang declares. Hold one alive across the
// vkCreateGraphicsPipelines call — `info` points into this object, so it is
// deliberately non-copyable and non-movable.
//
// Only available after vulkan.h has been included: it is the adapter at the
// renderer boundary, not part of the pure selection model.
#if defined(VK_VERSION_1_0)
struct OutputEncodeSpec {
    int32_t                  value;
    VkSpecializationMapEntry entry{};
    VkSpecializationInfo     info{};

    explicit OutputEncodeSpec(OutputEncode e) : value(static_cast<int32_t>(e)) {
        entry.constantID = 0;
        entry.offset     = 0;
        entry.size       = sizeof(int32_t);
        info.mapEntryCount = 1;
        info.pMapEntries   = &entry;
        info.dataSize      = sizeof(int32_t);
        info.pData         = &value;
    }
    OutputEncodeSpec(const OutputEncodeSpec&) = delete;
    OutputEncodeSpec& operator=(const OutputEncodeSpec&) = delete;

    const VkSpecializationInfo* get() const { return &info; }
};

// ── Vulkan adapter (compatibility overload) ─────────────────────────────────
//
// Available only when vulkan.h was included first, which is the case in every
// renderer TU. The pure picker above is what a platform-neutral test uses; the
// renderer converts the enumeration and the chosen pair here.

inline OutputFormat outputFormatFromVk(VkFormat f) {
    switch (f) {
        case VK_FORMAT_R8G8B8A8_UNORM:           return OutputFormat::R8G8B8A8Unorm;
        case VK_FORMAT_B8G8R8A8_UNORM:           return OutputFormat::B8G8R8A8Unorm;
        case VK_FORMAT_R16G16B16A16_SFLOAT:      return OutputFormat::R16G16B16A16Sfloat;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return OutputFormat::A2B10G10R10UnormPack32;
        case VK_FORMAT_UNDEFINED:                return OutputFormat::Undefined;
        default:                                 return OutputFormat::Undefined;
    }
}

inline OutputColorSpace outputColorSpaceFromVk(VkColorSpaceKHR c) {
    switch (c) {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:      return OutputColorSpace::SrgbNonlinear;
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return OutputColorSpace::ExtendedSrgbLinear;
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:        return OutputColorSpace::Bt2020Linear;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:         return OutputColorSpace::Hdr10St2084;
        case VK_COLOR_SPACE_PASS_THROUGH_EXT:         return OutputColorSpace::PassThrough;
        default:                                      return OutputColorSpace::SrgbNonlinear;
    }
}

inline VkFormat outputFormatToVk(OutputFormat f) {
    switch (f) {
        case OutputFormat::R8G8B8A8Unorm:           return VK_FORMAT_R8G8B8A8_UNORM;
        case OutputFormat::B8G8R8A8Unorm:           return VK_FORMAT_B8G8R8A8_UNORM;
        case OutputFormat::R16G16B16A16Sfloat:      return VK_FORMAT_R16G16B16A16_SFLOAT;
        case OutputFormat::A2B10G10R10UnormPack32:  return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        default:                                    return VK_FORMAT_UNDEFINED;
    }
}

inline VkColorSpaceKHR outputColorSpaceToVk(OutputColorSpace c) {
    switch (c) {
        case OutputColorSpace::SrgbNonlinear:    return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case OutputColorSpace::ExtendedSrgbLinear: return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        case OutputColorSpace::Bt2020Linear:      return VK_COLOR_SPACE_BT2020_LINEAR_EXT;
        case OutputColorSpace::Hdr10St2084:       return VK_COLOR_SPACE_HDR10_ST2084_EXT;
        case OutputColorSpace::PassThrough:       return VK_COLOR_SPACE_PASS_THROUGH_EXT;
    }
    return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
}

inline OutputSelection pickTarget(const VkSurfaceFormatKHR* formats, size_t count,
                                  bool extColorspace, OutputTarget requested) {
    std::vector<SurfaceFormat> converted;
    converted.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        converted.push_back({ outputFormatFromVk(formats[i].format),
                              outputColorSpaceFromVk(formats[i].colorSpace) });
    }
    return pickTarget(converted, extColorspace, requested);
}

inline OutputSelection pickTarget(const std::vector<VkSurfaceFormatKHR>& formats,
                                  bool extColorspace, OutputTarget requested) {
    return pickTarget(formats.data(), formats.size(), extColorspace, requested);
}

#endif // VK_VERSION_1_0
