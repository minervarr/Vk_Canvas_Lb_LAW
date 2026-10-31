#pragma once
// Small value types shared by the draw, layout and UI layers.
//
// This header must stay dependency-free: pure geometry and colour code should
// be testable without pulling in Canvas, the font engine or Vulkan.

struct Color {
  float r, g, b, a;
};

struct Rect {
  float x, y, w, h;
  bool contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
  }
};
