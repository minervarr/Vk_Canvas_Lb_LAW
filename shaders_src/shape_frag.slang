// SDF shape pipeline, fragment stage: evaluates the shape's analytic signed
// distance at this pixel and converts it to coverage — the same idea as the
// MSDF text pipeline, but with closed-form distance functions instead of a
// distance-field atlas. One pixel of smoothing on the true mathematical
// edge = native antialiasing, no compute pass, no intermediate image.
//
// Shape kinds (d0.x):
//   0 = rounded rect : center=(d0.y,d0.z) halfExt=(d0.w,d1.x) radius=d1.y
//   1 = capsule      : a=(d0.y,d0.z) b=(d0.w,d1.x) radius=d1.y
//   2 = triangle     : v0=(d0.y,d0.z) v1=(d0.w,d1.x) v2=(d1.y,d1.z)
//
// Output is straight alpha; the pipeline blends SRC_ALPHA / 1-SRC_ALPHA
// (same blend state as the MSDF text pipeline).

[shader("fragment")]
float4 main(float4 color : COLOR0, float4 d0 : TEXCOORD0, float4 d1 : TEXCOORD1,
            float2 pix : TEXCOORD2) : SV_Target {
  float d = 1e9;
  int kind = int(d0.x + 0.5);

  if (kind == 0) {
    // Rounded box (iq): shrink the box by the corner radius, add it back.
    float2 p = pix - float2(d0.y, d0.z);
    float2 b = float2(d0.w, d1.x) - d1.y;
    float2 q = abs(p) - b;
    d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - d1.y;
  } else if (kind == 1) {
    // Capsule: distance to segment minus radius.
    float2 a = float2(d0.y, d0.z);
    float2 bp = float2(d0.w, d1.x);
    float2 pa = pix - a, ba = bp - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    d = length(pa - ba * h) - d1.y;
  } else {
    // Triangle (iq's sdTriangle) — sign-correct for either winding order.
    float2 p0 = float2(d0.y, d0.z);
    float2 p1 = float2(d0.w, d1.x);
    float2 p2 = float2(d1.y, d1.z);
    float2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    float2 v0 = pix - p0, v1 = pix - p1, v2 = pix - p2;
    float2 pq0 = v0 - e0 * clamp(dot(v0, e0) / max(dot(e0, e0), 1e-6), 0.0, 1.0);
    float2 pq1 = v1 - e1 * clamp(dot(v1, e1) / max(dot(e1, e1), 1e-6), 0.0, 1.0);
    float2 pq2 = v2 - e2 * clamp(dot(v2, e2) / max(dot(e2, e2), 1e-6), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    float2 dm = min(min(float2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                        float2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                    float2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    d = -sqrt(dm.x) * sign(dm.y);
  }

  // Distance -> coverage across exactly one SCREEN pixel, whatever the
  // scale: fwidth(d) is how much the distance changes per screen pixel at
  // this fragment, so dividing by it normalizes the AA band to 1px under
  // any zoom/DPI/transform (identical to the old fixed `0.5 - d` at 1:1,
  // where fwidth(d) == 1).
  float w = max(fwidth(d), 1e-6);
  float cov = clamp(0.5 - d / w, 0.0, 1.0);
  return float4(color.rgb, color.a * cov);
}
