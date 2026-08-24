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
  float pad0; float pad1;
};

// Linear light -> sRGB. MANDATORY, and easy to forget: the swapchain is
// VK_FORMAT_R8G8B8A8_UNORM, not _SRGB, so nothing downstream encodes for us.
// Skipping it makes everything look far too dark.
float srgbEncode(float x) {
  return x <= 0.0031308 ? x * 12.92
                        : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

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
float rolloff(float x, float W) {
  const float k = 0.8;
  if (x <= k) return x;
  float span = max(W - k, 1e-4);
  float a = (x - k) / span;
  float d = span / (1.0 - k);
  return k + (1.0 - k) * (a * (1.0 + a / d) / (1.0 + a));
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

  // The historical path, bit-for-bit. Every consumer that predates tone
  // mapping uploads display-referred RGBA8 and gets premultiply and nothing
  // else -- no encode, no curve, no dither.
  if (toneMode < 0.5) return float4(c.rgb * c.a, c.a);

  // From here the texture is LINEAR light and may legitimately hold values
  // below 0 (out-of-gamut) or far above 1 (highlights).
  float3 lin = c.rgb * exposure;

  bool clipped = any(lin > 1.0);

  float3 t;
  if (toneMode < 1.5) {
    t = lin;                              // kClip: the literal truth
  } else {
    // kRolloff. Scale by a ratio computed from LUMINANCE rather than curving
    // each channel on its own: per-channel compression desaturates saturated
    // highlights toward white, turning a bright red into a pink one.
    float L = dot(lin, float3(0.2126, 0.7152, 0.0722));
    float s = L > 1e-6 ? rolloff(L, max(white, 1.0)) / L : 0.0;
    t = lin * s;
  }

  t = saturate(t);
  float3 enc = float3(srgbEncode(t.r), srgbEncode(t.g), srgbEncode(t.b));

  enc += (ignoise(fragPos.xy) - 0.5) / 255.0;
  enc = saturate(enc);

  // Diagonal stripes over anything the display cannot reach at this exposure.
  // Screen-space, so the pattern stays put while the image pans beneath it and
  // reads as an overlay rather than as part of the picture.
  if (clipWarn > 0.5 && clipped) {
    if (frac((fragPos.x + fragPos.y) * 0.08) < 0.5) enc = float3(1.0, 0.0, 1.0);
  }

  return float4(enc * c.a, c.a);
}
