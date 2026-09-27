// output_encode.slang — the single place every fragment stage turns a colour
// into the bytes the swapchain wants. Included by image/shape/overlay/msdf
// fragment shaders. See USAGE_hdr_output.md.
//
// OUTPUT_ENCODE is a SPECIALIZATION constant, not a uniform: the swapchain's
// colourspace is fixed for the life of a pipeline (pipelines are already
// rebuilt when swapchain_format_ changes), so the branch is resolved at
// pipeline-creation time and costs nothing per fragment. It must match
// Renderer::activeEncode() / the OutputEncode enum in core/output_target.hh —
// the values are the contract:
//
//   0 = Srgb         8-bit sRGB swapchain. Byte-for-byte the pre-HDR path.
//   1 = LinearScRgb  FP16 extended-linear. 1.0 == SDR reference white
//                    (~80 nits nominal, scaled by the OS); >1.0 is legal and
//                    is the entire point.
//   2 = PQ           10-bit ST 2084. Absolute luminance, 1.0 == 10000 nits.
[[vk::constant_id(0)]] const int OUTPUT_ENCODE = 0;

static const int kEncodeSrgb        = 0;
static const int kEncodeLinearScRgb = 1;
static const int kEncodePQ          = 2;

// Graphics white for HDR UI, BT.2408: UI authored as "white" should land at
// ~203 nits, NOT at the panel's peak. Rendering menus at 1000 nits is how HDR
// UI becomes painful to look at.
static const float kGraphicsWhiteNits = 203.0;
static const float kPQPeakNits        = 10000.0;

// ── sRGB transfer function ──────────────────────────────────────────────────
// Linear light -> sRGB. MANDATORY on an 8-bit UNORM swapchain (not _SRGB, so
// nothing downstream encodes for us); skipping it makes everything too dark.
float srgbEncode1(float x) {
  return x <= 0.0031308 ? x * 12.92
                        : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
float3 srgbEncode3(float3 c) {
  return float3(srgbEncode1(c.r), srgbEncode1(c.g), srgbEncode1(c.b));
}

// sRGB -> linear light. Needed for the UI paths: their colours are authored as
// display-referred sRGB numbers, which are meaningless written raw into a
// linear surface (that is the washed-out camera bug, from the other side).
float srgbDecode1(float x) {
  return x <= 0.04045 ? x / 12.92
                      : pow((x + 0.055) / 1.055, 2.4);
}
float3 srgbDecode3(float3 c) {
  return float3(srgbDecode1(c.r), srgbDecode1(c.g), srgbDecode1(c.b));
}

// ── ST 2084 (PQ) OETF ───────────────────────────────────────────────────────
// Input is absolute luminance normalized so 1.0 == kPQPeakNits.
//
// MIRRORS pqEncode() in core/output_target.cc, which is the AUTHORITY: this
// shader cannot be unit-tested (it needs slangc and a GPU), so the golden
// values in core/tests/output_target_test.cc guard the constants on the C++
// side only. Change one, change both — the same silent-drift hazard CLAUDE.md
// records for the font-engine shaders.
static const float kPQ_m1 = 0.1593017578125;   // 2610 / 16384
static const float kPQ_m2 = 78.84375;          // 2523 / 4096 * 128
static const float kPQ_c1 = 0.8359375;         // 3424 / 4096
static const float kPQ_c2 = 18.8515625;        // 2413 / 4096 * 32
static const float kPQ_c3 = 18.6875;           // 2392 / 4096 * 32

float pqEncode1(float y) {
  float yp = pow(max(y, 0.0), kPQ_m1);
  return pow((kPQ_c1 + kPQ_c2 * yp) / (1.0 + kPQ_c3 * yp), kPQ_m2);
}
float3 pqEncode3(float3 c) {
  return float3(pqEncode1(c.r), pqEncode1(c.g), pqEncode1(c.b));
}

// ── The two entry points ────────────────────────────────────────────────────

// UI colours (shapes, vector overlay, MSDF text, letterbox): authored as
// display-referred sRGB numbers in [0,1].
//
// Under Srgb this is the IDENTITY — the UI paths never encoded before and must
// stay byte-identical. Under the HDR targets the number has to be carried into
// the target's own space, capped at graphics white so UI never blazes.
float3 encodeUiColor(float3 srgbNumber) {
  if (OUTPUT_ENCODE == kEncodeLinearScRgb) {
    // 1.0 == SDR reference white, which is exactly what UI white means.
    return srgbDecode3(saturate(srgbNumber));
  }
  if (OUTPUT_ENCODE == kEncodePQ) {
    float3 lin = srgbDecode3(saturate(srgbNumber));
    return pqEncode3(lin * (kGraphicsWhiteNits / kPQPeakNits));
  }
  return srgbNumber;  // kEncodeSrgb: unchanged
}

// Image content: LINEAR light, already tone-mapped by the caller, where 1.0 is
// the caller's chosen display white. Unlike the UI path this may legitimately
// exceed 1.0 under an HDR target — that headroom is the whole feature.
//
// `whiteNits` is what the caller wants 1.0 to mean on an absolute-luminance
// target (PQ); it is ignored by the relative targets.
float3 encodeImageLinear(float3 lin, float whiteNits) {
  if (OUTPUT_ENCODE == kEncodeLinearScRgb) {
    // No saturate: >1.0 is the headroom. Clamp only the negative side, which
    // is out-of-gamut nonsense rather than brightness.
    return max(lin, 0.0);
  }
  if (OUTPUT_ENCODE == kEncodePQ) {
    return pqEncode3(max(lin, 0.0) * (whiteNits / kPQPeakNits));
  }
  return srgbEncode3(saturate(lin));  // kEncodeSrgb
}

// Where "beyond what this target can show" starts, in the same linear units
// encodeImageLinear() takes. Drives image_frag's clipWarn stripes: on SDR that
// is 1.0 (display white), on an HDR target the headroom above white is real,
// so warning at 1.0 would stripe every correctly-bright highlight.
float outputClipThreshold(float headroom) {
  if (OUTPUT_ENCODE == kEncodeSrgb) return 1.0;
  return max(headroom, 1.0);
}

// True when the encode leaves values above 1.0 meaningful (so callers must not
// saturate, and dither — a 1/255 8-bit trick — is pointless).
bool outputIsExtendedRange() { return OUTPUT_ENCODE != kEncodeSrgb; }
