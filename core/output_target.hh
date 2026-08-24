#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>
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

// How a fragment stage must encode its output for the resolved target. Mirrors
// the OUTPUT_ENCODE specialization constant consumed by the shaders.
enum class OutputEncode {
    Srgb = 0,       // encode + dither + saturate (current behavior, byte-exact)
    LinearScRgb = 1,// linearize authored UI colors; image path skips saturate
    PQ = 2,         // tone-map to target range, then ST 2084 OETF
};

struct OutputSelection {
    VkFormat        format     = VK_FORMAT_R8G8B8A8_UNORM;
    VkColorSpaceKHR colorspace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
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

// Resolve `requested` against one vkGetPhysicalDeviceSurfaceFormatsKHR
// enumeration. `extColorspace` is whether VK_EXT_swapchain_colorspace was
// enabled at instance level — without it, only SRGB_NONLINEAR is meaningful,
// so every HDR request falls back regardless of what the list claims.
//
// A single {VK_FORMAT_UNDEFINED, ...} entry (the legacy "any format" reply)
// is treated as "the SDR pin is available", nothing more.
OutputSelection pickTarget(const VkSurfaceFormatKHR* formats, size_t count,
                           bool extColorspace, OutputTarget requested);

inline OutputSelection pickTarget(const std::vector<VkSurfaceFormatKHR>& formats,
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
