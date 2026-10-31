#pragma once

// Minimal per-frame animation primitive: a float that interpolates from its
// current value to a target over a fixed duration with a pluggable easing
// function. The renderer has no animation concept — callers own their
// AnimatedFloat instances and call update(dtSeconds) once per frame, then read
// value() when building the scene. Pure logic, no platform/Vulkan includes.

using EaseFn = float (*)(float);

float easeLinear(float t);
float easeInOutCubic(float t);

class AnimatedFloat {
 public:
  explicit AnimatedFloat(float initial = 0.0f)
      : current_(initial), from_(initial), to_(initial) {}

  // Restarts the animation from the *current* value toward `target`.
  void set(float target, float durationSeconds, EaseFn ease = easeLinear) {
    from_ = current_;
    to_ = target;
    duration_ = durationSeconds;
    elapsed_ = 0.0f;
    ease_ = ease;
  }

  void update(float dtSeconds) {
    if (elapsed_ >= duration_) { current_ = to_; return; }
    elapsed_ += dtSeconds;
    if (elapsed_ >= duration_) { current_ = to_; return; }  // clamp, never extrapolate
    float t = duration_ > 0.0f ? elapsed_ / duration_ : 1.0f;
    current_ = from_ + (to_ - from_) * ease_(t);
  }

  float value() const { return current_; }
  bool isAnimating() const { return elapsed_ < duration_; }

 private:
  float current_, from_, to_;
  float elapsed_ = 0.0f, duration_ = 0.0f;
  EaseFn ease_ = easeLinear;
};
