#include "output_encode.slang"

[[vk::binding(0, 0)]] Sampler2D tex;

// ── The push-constant block ─────────────────────────────────────────────────
//
// This block MUST be character-identical in image_vert.slang and
// image_frag.slang. Slang assigns offsets by declaration order within the
// block, so a field added to one file and not the other silently shifts every
// later field in that stage only -- the fragment shader then reads the vertex
// shader's dstW as its exposure, which looks like a geometry bug and is not.
//
// 16 floats = 64 bytes, half of the 128 bytes Vulkan guarantees.
[[vk::push_constant]] cbuffer PC {
  float dstX; float dstY; float dstW; float dstH;
  float u0;   float v0;   float u1;   float v1;
  float screenW; float screenH;
  float exposure; float toneMode; float white; float clipWarn;
  float whiteNits; float headroom;
};


// Extended Reinhard with a linear knee.
//
// Below k the curve is the IDENTITY, so everything an ordinary SDR image
// contains passes through untouched and a plain sRGB photo renders exactly as
// it would have before any of this existed. Only the range above k -- which an
// 8-bit display cannot show at all -- is compressed, and it is compressed
// monotonically, so brighter input always stays brighter output.
//
// Deliberately NOT ACES or a filmic S-curve. Those crush shadows, add
// contrast and shift hue: grading decisions, appropriate for a camera and
// wrong for an instrument. Someone opening this to inspect a photograph needs
// the midtones to be numerically what the file says.
//
// MIRRORS rolloffCurve() in core/output_target.cc, which is the AUTHORITY --
// same arrangement as the PQ constants above it. This shader cannot be
// unit-tested (it needs slangc plus a GPU), so the maths is guarded on the C++
// side in core/tests/output_target_test.cc. Change one, change both.
//
// P is the OUTPUT CEILING -- what the brightest input maps to. On an SDR
// target it is 1.0 and this is exactly the curve that existed before, term for
// term. On an HDR target it is the display's headroom, and that distinction is
// load-bearing rather than cosmetic: with the ceiling pinned at 1.0 the rolloff
// path can never emit a value above display white, so an HDR swapchain would
// be requested, granted, and then handed nothing but SDR-range pixels. The
// feature would be inert in the one tone mode a photograph actually uses.
float rolloff(float x, float W, float P) {
  const float k = 0.8;
  if (x <= k) return x;
  float span = max(W - k, 1e-4);
  float a = (x - k) / span;
  float d = span / max(P - k, 1e-4);
  return k + (P - k) * (a * (1.0 + a / d) / (1.0 + a));
}

// Interleaved-gradient noise, used as a +/-0.5 LSB dither before the 8-bit
// swapchain quantises. Exposing UP stretches a compressed range across the
// same 256 codes, which bands visibly in skies; this trades that banding for
// noise below the code step. It adds noise rather than inventing detail, which
// is the honest trade.
float ignoise(float2 p) {
  return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

[shader("fragment")]
float4 main(float2 uv : TEXCOORD0, float4 fragPos : SV_Position) : SV_Target {
  float4 c = tex.Sample(uv);

  // The historical path, bit-for-bit on the SDR target. Every consumer that
  // predates tone mapping uploads display-referred RGBA8 and gets premultiply
  // and nothing else -- no curve, no dither. Under an HDR target those
  // display-referred numbers still have to be carried into the target's space
  // (encodeUiColor is the identity on SDR, so this stays byte-exact there).
  if (toneMode < 0.5) {
    float3 sdr = encodeUiColor(c.rgb);
    return float4(sdr * c.a, c.a);
  }

  // From here the texture is LINEAR light and may legitimately hold values
  // below 0 and far above 1.
  //
  // The NEGATIVES ARE CLAMPED FIRST, and that ordering is load-bearing rather
  // than tidy-minded. A colour outside sRGB's gamut -- which is most of what a
  // BT.2100 photo's saturated highlights are -- comes back from the colour
  // conversion with a negative component. Feed that to the luminance dot
  // product below and L collapses toward zero, so rolloff(L)/L explodes, the
  // positive channels slam to 1.0 and the negative one clamps to 0. Bright
  // specular reflections came out MAGENTA with a green fringe, which reads as
  // a colour-conversion bug and is really an operator-ordering one. Measured
  // on a real Rec.2100 photo: 63441 pixels had a negative component, and
  // exactly 63441 pixels turned magenta.
  //
  // Clamping here is not data loss: the value is outside what the display can
  // reproduce either way, and the full range is still in the texture for the
  // exposure control to reach.
  float3 lin = max(c.rgb * exposure, 0.0);

  // "Beyond what THIS target can show". On SDR that is display white; on an
  // HDR target the headroom above white is real, so striping at 1.0 would
  // false-alarm on every correctly-bright highlight. `headroom` is the
  // consumer's display-relative peak (see ImageDraw::headroom).
  float ceiling = outputClipThreshold(headroom);
  bool clipped = any(lin > ceiling);

  float3 t;
  if (toneMode < 1.5) {
    t = lin;                              // kClip: the literal truth
  } else {
    // kRolloff. Scale by a ratio computed from LUMINANCE rather than curving
    // each channel on its own: per-channel compression desaturates saturated
    // highlights toward white, turning a bright red into a pink one.
    float L = dot(lin, float3(0.2126, 0.7152, 0.0722));
    float s = L > 1e-6 ? rolloff(L, max(white, 1.0), ceiling) / L : 0.0;
    t = lin * s;
  }

  // Anything still over the ceiling is desaturated TOWARD WHITE rather than
  // clipped per channel. A per-channel clamp changes the hue of a blown
  // highlight -- the brightest channel stops growing while the others keep
  // going -- which is the second half of the magenta artefact and is why a
  // clipped specular used to acquire a colour it never had. This maps the
  // largest channel to exactly the ceiling and pulls the others up with it, so
  // a blown highlight goes white, which is what a blown highlight looks like.
  //
  // kClip deliberately does NOT get this treatment: its whole contract is to
  // show the literal truth, and a hard clip is that truth.
  if (toneMode > 1.5) {
    float m = max(t.r, max(t.g, t.b));
    if (m > ceiling) t = lerp(t, float3(ceiling, ceiling, ceiling), 1.0 - ceiling / m);
  }

  // Encode for whatever swapchain this pipeline was specialized against. The
  // SDR path saturates and sRGB-encodes exactly as before; the HDR paths keep
  // values above display white, which is the entire point of the feature.
  float3 enc = encodeImageLinear(t, max(whiteNits, 1.0));

  // Ordered dither breaks up 8-bit banding in smooth gradients. It is a
  // 1/255-of-a-code-value trick, so it only means anything on the 8-bit SDR
  // target — on FP16/10-bit it would just add noise.
  if (!outputIsExtendedRange()) {
    enc += (ignoise(fragPos.xy) - 0.5) / 255.0;
    enc = saturate(enc);
  }

  // Diagonal stripes over anything the display cannot reach at this exposure.
  // Screen-space, so the pattern stays put while the image pans beneath it and
  // reads as an overlay rather than as part of the picture.
  if (clipWarn > 0.5 && clipped) {
    if (frac((fragPos.x + fragPos.y) * 0.08) < 0.5)
      enc = encodeUiColor(float3(1.0, 0.0, 1.0));  // UI marker, not content
  }

  return float4(enc * c.a, c.a);
}
