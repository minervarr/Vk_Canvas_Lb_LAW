// Samples an app-uploaded RGBA texture (straight alpha, e.g. from stb_image)
// and premultiplies it here so it composites correctly against the pipeline's
// premultiplied-alpha blend state (srcFactor ONE, dstFactor 1-srcA) — the same
// blend equation overlay_frag.slang's UI pass uses.
[[vk::binding(0, 0)]] Sampler2D tex;

[shader("fragment")]
float4 main(float2 uv : TEXCOORD0) : SV_Target {
  float4 c = tex.Sample(uv);
  return float4(c.rgb * c.a, c.a);
}
