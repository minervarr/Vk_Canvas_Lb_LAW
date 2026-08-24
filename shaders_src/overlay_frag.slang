// Samples the overlay image (premultiplied alpha, written by the coverage
// compute pass) and returns it; the pipeline blends it over the camera with
// premultiplied-alpha blending (srcFactor ONE, dstFactor 1-srcA).
//
// The overlay is drawn unrotated; here we rotate the SAMPLING about the image
// centre (in pixel space, so there is no aspect distortion) by the device
// orientation. cosA/sinA are the inverse (sampling) rotation; w/h are the image
// size in pixels. Samples that fall outside the image are transparent.
#include "output_encode.slang"

[[vk::binding(0, 0)]] Sampler2D overlayImage;

[[vk::push_constant]] cbuffer PC { float cosA; float sinA; float w; float h; };

[shader("fragment")]
float4 main(float2 uv : TEXCOORD0) : SV_Target {
  float2 size = float2(w, h);
  float2 ctr  = size * 0.5;
  float2 d    = uv * size - ctr;
  float2 src  = ctr + float2(cosA * d.x - sinA * d.y,
                             sinA * d.x + cosA * d.y);
  float2 suv  = src / size;
  if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
    return float4(0.0, 0.0, 0.0, 0.0);
  float4 s4 = overlayImage.Sample(suv);

  // SDR: hand the sample back untouched. encodeUiColor() is the identity here,
  // but the premultiply round-trip below is NOT — (rgb/a)*a does not reproduce
  // rgb bit-exactly in floating point, and the alpha guard would drop pixels
  // that used to pass through. This path predates the HDR work and must stay
  // byte-for-byte what it was, so it returns before any of that.
  if (OUTPUT_ENCODE == kEncodeSrgb) return s4;

  // HDR: the overlay image holds PREMULTIPLIED display-referred sRGB numbers,
  // and the encode is defined on UNPREMULTIPLIED colour (it is a non-linear
  // curve, so encode(c*a) != encode(c)*a). Undo the premultiply, encode, redo
  // it. Fully transparent texels carry no colour to recover.
  if (s4.a <= 1e-5) return float4(0.0, 0.0, 0.0, 0.0);
  return float4(encodeUiColor(s4.rgb / s4.a) * s4.a, s4.a);
}
