struct VertexOutput {
  float4 position : SV_Position;
  float2 uv       : TEXCOORD0;
};

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

[shader("vertex")]
VertexOutput main(uint vertexId : SV_VertexID) {
  float cx = float(vertexId & 1u);
  float cy = float((vertexId >> 1) & 1u);

  VertexOutput o;
  o.uv = float2(u0 + cx * (u1 - u0), v0 + cy * (v1 - v0));
  float2 pix = float2(dstX + cx * dstW, dstY + cy * dstH);
  float2 ndc = (pix / float2(screenW, screenH)) * 2.0 - 1.0;
  o.position = float4(ndc, 0.0, 1.0);
  return o;
}
