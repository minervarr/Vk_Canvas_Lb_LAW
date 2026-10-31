// Pure: no Vulkan device, header or link dependency. The renderer converts the
// local SurfaceFormat to VkSurfaceFormatKHR at the swapchain boundary.
// Asserts must stay live even though the library builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "output_target.hh"

static std::vector<SurfaceFormat> sdrOnly() {
  return {{OutputFormat::B8G8R8A8Unorm, OutputColorSpace::SrgbNonlinear},
          {OutputFormat::R8G8B8A8Unorm, OutputColorSpace::SrgbNonlinear}};
}

static bool isSdrPin(const OutputSelection& s) {
  return s.format == OutputFormat::R8G8B8A8Unorm &&
         s.colorspace == OutputColorSpace::SrgbNonlinear &&
         s.target == OutputTarget::SdrSrgb && s.encode == OutputEncode::Srgb &&
         !s.hdr && !s.degraded;
}

int main() {
  // The regression proof for every existing (camera) consumer: an SdrSrgb
  // request resolves to exactly the pre-HDR pin, whatever the device
  // enumerates and whether or not the colorspace extension is around.
  {
    std::vector<SurfaceFormat> rich = {
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::ExtendedSrgbLinear},
        {OutputFormat::A2B10G10R10UnormPack32, OutputColorSpace::Hdr10St2084},
        {OutputFormat::R8G8B8A8Unorm, OutputColorSpace::SrgbNonlinear}};
    OutputSelection s = pickTarget(rich, true, OutputTarget::SdrSrgb);
    assert(isSdrPin(s) && !s.fellBack);  // not a fallback — what was asked for
    assert(isSdrPin(pickTarget(rich, false, OutputTarget::SdrSrgb)));
    assert(isSdrPin(pickTarget(sdrOnly(), true, OutputTarget::SdrSrgb)));
    assert(isSdrPin(pickTarget(nullptr, 0, true, OutputTarget::SdrSrgb)));
  }

  // ExtendedLinearScrgb: FP16 + EXTENDED_SRGB_LINEAR is the first preference.
  {
    std::vector<SurfaceFormat> f = {
        {OutputFormat::R8G8B8A8Unorm, OutputColorSpace::SrgbNonlinear},
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::Bt2020Linear},
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::ExtendedSrgbLinear}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(s.format == OutputFormat::R16G16B16A16Sfloat);
    assert(s.colorspace == OutputColorSpace::ExtendedSrgbLinear);
    assert(s.encode == OutputEncode::LinearScRgb);
    assert(s.hdr && !s.fellBack && !s.degraded);
    assert(s.target == OutputTarget::ExtendedLinearScrgb);
  }

  // ...and BT2020_LINEAR is the second, taken only when the first is absent.
  {
    std::vector<SurfaceFormat> f = {
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::Bt2020Linear}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(s.colorspace == OutputColorSpace::Bt2020Linear);
    assert(s.hdr && !s.fellBack);
  }

  // Hdr10PQ: ST2084 preferred; PASS_THROUGH accepted but flagged degraded.
  {
    std::vector<SurfaceFormat> f = {
        {OutputFormat::A2B10G10R10UnormPack32, OutputColorSpace::PassThrough},
        {OutputFormat::A2B10G10R10UnormPack32, OutputColorSpace::Hdr10St2084}};
    OutputSelection s = pickTarget(f, true, OutputTarget::Hdr10PQ);
    assert(s.colorspace == OutputColorSpace::Hdr10St2084);
    assert(s.encode == OutputEncode::PQ && s.hdr && !s.degraded);

    std::vector<SurfaceFormat> pt = {
        {OutputFormat::A2B10G10R10UnormPack32, OutputColorSpace::PassThrough}};
    OutputSelection d = pickTarget(pt, true, OutputTarget::Hdr10PQ);
    assert(d.colorspace == OutputColorSpace::PassThrough);
    assert(d.hdr && d.degraded && !d.fellBack);
  }

  // Format and colorspace must match as a *pair*: the right format under the
  // wrong colorspace (or vice versa) is not a match.
  {
    std::vector<SurfaceFormat> f = {
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::SrgbNonlinear},
        {OutputFormat::R8G8B8A8Unorm, OutputColorSpace::ExtendedSrgbLinear}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(isSdrPin(s) && s.fellBack);
  }

  // Missing VK_EXT_swapchain_colorspace: fall back even if the list claims HDR
  // pairs — those enums are only valid with the instance extension enabled.
  {
    std::vector<SurfaceFormat> f = {
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::ExtendedSrgbLinear},
        {OutputFormat::A2B10G10R10UnormPack32, OutputColorSpace::Hdr10St2084}};
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

  // Legacy "any format" reply (a single OutputFormat::Undefined entry) advertises
  // no colorspace but SRGB_NONLINEAR, so HDR requests fall back.
  {
    std::vector<SurfaceFormat> f = {
        {OutputFormat::Undefined, OutputColorSpace::SrgbNonlinear}};
    assert(isSdrPin(pickTarget(f, true, OutputTarget::SdrSrgb)));
    OutputSelection s = pickTarget(f, true, OutputTarget::Hdr10PQ);
    assert(isSdrPin(s) && s.fellBack);
  }

  // A stray UNDEFINED among real entries is NOT the legacy reply (that is
  // defined as a single-entry list), so the genuine pairs after it still count.
  {
    std::vector<SurfaceFormat> f = {
        {OutputFormat::Undefined, OutputColorSpace::SrgbNonlinear},
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::ExtendedSrgbLinear}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(s.hdr && !s.fellBack);
    assert(s.colorspace == OutputColorSpace::ExtendedSrgbLinear);
  }

  // Duplicate entries are harmless — first match still wins, same result.
  {
    std::vector<SurfaceFormat> f = {
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::ExtendedSrgbLinear},
        {OutputFormat::R16G16B16A16Sfloat, OutputColorSpace::ExtendedSrgbLinear}};
    OutputSelection s = pickTarget(f, true, OutputTarget::ExtendedLinearScrgb);
    assert(s.hdr && s.colorspace == OutputColorSpace::ExtendedSrgbLinear);
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

  // ── The rolloff curve ─────────────────────────────────────────────────────
  // rolloffCurve() gained a `ceiling` parameter so highlights can roll toward
  // an HDR panel's headroom instead of always toward display white. These
  // guard the shader's copy in image_frag.slang, which cannot be tested.
  {
    // The REGRESSION PROOF for every SDR consumer: at ceiling == 1.0 this is
    // the pre-HDR curve bit-for-bit -- equality, not a tolerance.
    const float k = 0.8f;
    for (float W : {1.0f, 1.5f, 2.0f, 4.0f, 10.0f, 100.0f}) {
      for (float x = 0.0f; x <= 200.0f; x += 0.005f) {
        float expect;
        if (x <= k) {
          expect = x;
        } else {
          float span = std::fmax(W - k, 1e-4f);
          float a = (x - k) / span;
          float d = span / (1.0f - k);
          expect = k + (1.0f - k) * (a * (1.0f + a / d) / (1.0f + a));
        }
        assert(rolloffCurve(x, W, 1.0f) == expect);
      }
    }
    for (float P : {1.0f, 2.0f, 4.0f, 8.0f}) {
      for (float W : {1.5f, 4.0f, 10.0f}) {
        // Below the knee: the identity at EVERY ceiling. An SDR photo's
        // midtones stay numerically what the file says, HDR target or not.
        for (float x = 0.0f; x <= 0.8f; x += 0.01f)
          assert(rolloffCurve(x, W, P) == x);

        // Monotone: brighter input always stays brighter output.
        float prev = -1e9f;
        for (float x = 0.0f; x <= 4.0f * W; x += 0.002f) {
          float v = rolloffCurve(x, W, P);
          assert(v >= prev - 1e-6f);
          prev = v;
        }

        // The point of the parameter: more headroom compresses LESS, so
        // highlights reach further up the panel instead of being crushed
        // toward SDR white.
        for (float x = 0.81f; x <= W; x += 0.01f)
          assert(rolloffCurve(x, W, P) >= rolloffCurve(x, W, 1.0f) - 1e-6f);

        // The curve does reach the ceiling. It is unbounded, so "reaches" is a
        // crossing, analytically at a == sqrt(span / (ceiling - k)).
        //
        // Only meaningful when the ceiling is genuinely BELOW the image's white
        // point, i.e. when there is something to compress. A ceiling at or
        // above `white` is clamped to it and the curve is the identity, which
        // never "reaches" anything -- see the never-amplifies block below.
        if (P > 1.0f && P < W) {
          float span = W - k;
          float xc = k + span * std::sqrt(span / (P - k));
          assert(std::fabs(rolloffCurve(xc, W, P) - P) < 1e-3f);
        }
      }
    }
  }

  {
    // NEVER AMPLIFIES. This curve compresses; it must not be able to make a
    // pixel brighter than it came in, at any ceiling.
    //
    // Regression: the ceiling was briefly allowed to exceed the image's own
    // white point, and the curve then EXTRAPOLATED instead of compressing --
    // a photo with white == 1.37 on a panel with headroom 2.22 had its
    // highlights multiplied by up to 2.24x and overshot the ceiling itself
    // (1.37 -> 3.279 against a ceiling of 2.22). On a device that looked like
    // coloured speckle over every specular highlight, because the runaway
    // values then hit the desaturate-toward-white step channel by channel.
    for (float P : {1.0f, 1.37f, 2.22f, 8.0f})
      for (float W : {1.0f, 1.37f, 4.0f, 10.0f})
        for (float x = 0.0f; x <= 2.0f * W; x += 0.001f)
          assert(rolloffCurve(x, W, P) <= x + 1e-6f);

    // An image that already fits the target's headroom passes through
    // UNTOUCHED. This is the ordinary case on an HDR panel and the whole point
    // of asking for one.
    //
    // Identity to within TWO float ULP, not bit-exact, and the difference is
    // arithmetic rather than behavioural: with the ceiling clamped to `white`
    // the expression collapses to k + span * ((x - k) / span), whose round
    // trip through the division costs a last-place bit or two. Measured worst
    // case over this sweep is 2.384e-07 at x = 1.842, W = 2.0 -- which is 2
    // ULP at that magnitude.
    for (float W : {1.0f, 1.37f, 2.0f}) {
      for (float x = 0.0f; x <= W; x += 0.001f) {
        float y = rolloffCurve(x, W, /*ceiling=*/W + 1.0f);
        assert(std::fabs(y - x) <= 2.5e-7f * std::fmax(x, 1.0f));
      }
    }

    // ...including an SDR image on an HDR panel: headroom is not licence to
    // stretch a picture that never had the range.
    for (float x = 0.0f; x <= 1.0f; x += 0.001f)
      assert(std::fabs(rolloffCurve(x, 1.0f, 2.22f) - x) <= 2.5e-7f);

  }

  printf("output_target_test: OK\n");
  return 0;
}
