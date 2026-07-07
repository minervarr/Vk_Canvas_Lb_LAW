// Draws one textured quad (album art, icons) at an arbitrary screen rect with
// a UV sub-rect, via push constants — no vertex buffer. 4 vertices, triangle
// strip: vertexId bits give (0,0) (1,0) (0,1) (1,1) — the standard bit-trick
// quad (same idiom as overlay_vert.slang's fullscreen triangle), no dynamic
// array indexing.
struct VertexOutput {
  float4 position : SV_Position;
  float2 uv       : TEXCOORD0;
};

[[vk::push_constant]] cbuffer PC {
  float dstX; float dstY; float dstW; float dstH;
  float u0;   float v0;   float u1;   float v1;
  float screenW; float screenH;
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
