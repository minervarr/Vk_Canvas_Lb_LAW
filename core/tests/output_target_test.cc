// Asserts must stay live even though the library builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "output_target.hh"

static std::vector<VkSurfaceFormatKHR> sdrOnly() {
  return {{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
          {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
}

static bool isSdrPin(const OutputSelection& s) {
  return s.format == VK_FORMAT_R8G8B8A8_UNORM &&
         s.colorspace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
         s.target == OutputTarget::SdrSrgb && s.encode == OutputEncode::Srgb &&
         !s.hdr && !s.degraded;
}

int main() {
  // The regression proof for every existing (camera) consumer: an SdrSrgb
  // request resolves to exactly the pre-HDR pin, whatever the device
  // enumerates and whether or not the colorspace extension is around.
  {
    std::vector<VkSurfaceFormatKHR> rich = {
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    OutputSelection s = pickTarget(rich, true, OutputTarget::SdrSrgb);
    assert(isSdrPin(s) && !s.fellBack);  // not a fallback — what was asked for
    assert(isSdrPin(pickTarget(rich, false, OutputTarget::SdrSrgb)));
    assert(isSdrPin(pickTarget(sdrOnly(), true, OutputTarget::SdrSrgb)));
    assert(isSdrPin(pickTarget(nullptr, 0, true, OutputTarget::SdrSrgb)));
  }

  // ExtendedLinearScrgb: FP16 + EXTENDED_SRGB_LINEAR is the first preference.
  {
    std::vector<VkSurfaceFormatKHR> f = {
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_BT2020_LINEAR_EXT},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(s.format == VK_FORMAT_R16G16B16A16_SFLOAT);
    assert(s.colorspace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
    assert(s.encode == OutputEncode::LinearScRgb);
    assert(s.hdr && !s.fellBack && !s.degraded);
    assert(s.target == OutputTarget::ExtendedLinearScrgb);
  }

  // ...and BT2020_LINEAR is the second, taken only when the first is absent.
  {
    std::vector<VkSurfaceFormatKHR> f = {
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_BT2020_LINEAR_EXT}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(s.colorspace == VK_COLOR_SPACE_BT2020_LINEAR_EXT);
    assert(s.hdr && !s.fellBack);
  }

  // Hdr10PQ: ST2084 preferred; PASS_THROUGH accepted but flagged degraded.
  {
    std::vector<VkSurfaceFormatKHR> f = {
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_PASS_THROUGH_EXT},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT}};
    OutputSelection s = pickTarget(f, true, OutputTarget::Hdr10PQ);
    assert(s.colorspace == VK_COLOR_SPACE_HDR10_ST2084_EXT);
    assert(s.encode == OutputEncode::PQ && s.hdr && !s.degraded);

    std::vector<VkSurfaceFormatKHR> pt = {
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_PASS_THROUGH_EXT}};
    OutputSelection d = pickTarget(pt, true, OutputTarget::Hdr10PQ);
    assert(d.colorspace == VK_COLOR_SPACE_PASS_THROUGH_EXT);
    assert(d.hdr && d.degraded && !d.fellBack);
  }

  // Format and colorspace must match as a *pair*: the right format under the
  // wrong colorspace (or vice versa) is not a match.
  {
    std::vector<VkSurfaceFormatKHR> f = {
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(isSdrPin(s) && s.fellBack);
  }

  // Missing VK_EXT_swapchain_colorspace: fall back even if the list claims HDR
  // pairs — those enums are only valid with the instance extension enabled.
  {
    std::vector<VkSurfaceFormatKHR> f = {
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT}};
    OutputSelection a = pickTarget(f, false, OutputTarget::ExtendedLinearScrgb);
    OutputSelection b = pickTarget(f, false, OutputTarget::Hdr10PQ);
    assert(isSdrPin(a) && a.fellBack);
    assert(isSdrPin(b) && b.fellBack);
  }

  // Empty enumeration, and an SDR-only device: HDR requests fall back.
  {
    OutputSelection e = pickTarget(nullptr, 0, true, OutputTarget::Hdr10PQ);
    assert(isSdrPin(e) && e.fellBack);
    OutputSelection s = pickTarget(sdrOnly(), true, OutputTarget::ExtendedLinearScrgb);
    assert(isSdrPin(s) && s.fellBack);
  }

  // Legacy "any format" reply (a single VK_FORMAT_UNDEFINED entry) advertises
  // no colorspace but SRGB_NONLINEAR, so HDR requests fall back.
  {
    std::vector<VkSurfaceFormatKHR> f = {
        {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    assert(isSdrPin(pickTarget(f, true, OutputTarget::SdrSrgb)));
    OutputSelection s = pickTarget(f, true, OutputTarget::Hdr10PQ);
    assert(isSdrPin(s) && s.fellBack);
  }

  // A stray UNDEFINED among real entries is NOT the legacy reply (that is
  // defined as a single-entry list), so the genuine pairs after it still count.
  {
    std::vector<VkSurfaceFormatKHR> f = {
        {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(s.hdr && !s.fellBack);
    assert(s.colorspace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
  }

  // Duplicate entries are harmless — first match still wins, same result.
  {
    std::vector<VkSurfaceFormatKHR> f = {
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(s.hdr && s.colorspace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
  }

  // ── PQ (ST 2084) golden values ────────────────────────────────────────────
  //
  // These guard the constants that shaders_src/output_encode.slang mirrors. The
  // shader itself is NOT tested — it needs slangc and a GPU — so this is the
  // only automated check that the PQ numbers are right anywhere in the tree.
  // Anchors are the published ST.2084 curve; a mismatch here means the shader
  // is wrong too.
  {
    auto nits = [](float n) { return pqEncode(n / 10000.0f); };
    assert(std::fabs(nits(100.0f)   - 0.508078f) < 1e-4f);
    assert(std::fabs(nits(203.0f)   - 0.580689f) < 1e-4f);  // graphics white
    assert(std::fabs(nits(1000.0f)  - 0.751827f) < 1e-4f);
    assert(std::fabs(nits(10000.0f) - 1.0f)      < 1e-4f);  // peak == 1.0

    // The OETF has a tiny non-zero floor at black (c1^m2), not exactly 0.
    assert(pqEncode(0.0f) >= 0.0f && pqEncode(0.0f) < 2e-6f);

    // Negative input is clamped, not NaN — out-of-gamut channels reach here.
    assert(pqEncode(-0.5f) == pqEncode(0.0f));

    // Monotonic across the whole range: no fold-back anywhere in the curve.
    float prev = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
      float v = pqEncode(float(i) / 1000.0f);
      assert(v > prev);
      assert(v >= 0.0f && v <= 1.0f + 1e-5f);
      prev = v;
    }

    // The named constants must agree with the curve they describe.
    assert(std::fabs(pqEncode(kPQPeakNits / kPQPeakNits) - 1.0f) < 1e-4f);
    assert(std::fabs(pqEncode(kGraphicsWhiteNits / kPQPeakNits) - 0.580689f) < 1e-4f);
  }

  printf("output_target_test: OK\n");
  return 0;
}
