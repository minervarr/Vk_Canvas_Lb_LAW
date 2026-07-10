// SDF shape pipeline, vertex stage: passes each quad vertex's pixel position
// through to NDC and forwards the per-shape SDF parameters (kind + geometry,
// packed by Canvas's shape emitter — see canvas.cc emitShapeQuad_) to the
// fragment stage. All 6 vertices of a shape's quad carry identical color/
// data0/data1, so plain interpolation is exact.
struct VSIn {
  float2 pos   : POSITION;    // screen pixels
  float4 color : COLOR0;      // straight (non-premultiplied) rgba
  float4 d0    : TEXCOORD0;   // kind + first 3 geometry params
  float4 d1    : TEXCOORD1;   // remaining geometry params
};

struct VSOut {
  float4 position : SV_Position;
  float4 color    : COLOR0;
  float4 d0       : TEXCOORD0;
  float4 d1       : TEXCOORD1;
  float2 pix      : TEXCOORD2;  // pixel-space position for SDF evaluation
};

[[vk::push_constant]] cbuffer PC {
  float screenW; float screenH;
};

[shader("vertex")]
VSOut main(VSIn i) {
  VSOut o;
  o.pix = i.pos;
  float2 ndc = (i.pos / float2(screenW, screenH)) * 2.0 - 1.0;
  o.position = float4(ndc, 0.0, 1.0);
  o.color = i.color;
  o.d0 = i.d0;
  o.d1 = i.d1;
  return o;
}
