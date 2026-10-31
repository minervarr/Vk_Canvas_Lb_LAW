#include "animated_float.hh"

#include <cmath>

float easeLinear(float t) { return t; }

float easeInOutCubic(float t) {
  return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
