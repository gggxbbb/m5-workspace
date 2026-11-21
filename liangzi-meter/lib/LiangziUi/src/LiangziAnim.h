#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace liangzi {

// Damped harmonic oscillator for spring animations (visualDuration + bounce)
// without external STL/RTTI overhead.
class SpringValue {
 public:
  SpringValue() = default;
  SpringValue(float initial, float visualDuration, float bounce)
      : value_(initial), target_(initial) {
    setOptions(visualDuration, bounce);
  }

  void setOptions(float visualDuration, float bounce) {
    constexpr float kTwoPi = 6.28318530718f;
    const float dur = std::max(0.04f, visualDuration);
    const float w = kTwoPi / dur;
    const float zeta = std::max(0.08f, 1.0f - bounce);
    stiffness_ = w * w;
    damping_ = 2.0f * zeta * w;
  }

  void setTarget(float target) { target_ = target; }

  void snap(float value) {
    value_ = value;
    target_ = value;
    velocity_ = 0.0f;
  }

  bool step(float dtSec) {
    if (!isMoving()) return false;
    const float dt = std::min(std::max(dtSec, 0.001f), 0.04f);
    const float force = (target_ - value_) * stiffness_;
    const float accel = force - velocity_ * damping_;
    velocity_ += accel * dt;
    value_ += velocity_ * dt;
    if (std::fabs(target_ - value_) < 0.002f && std::fabs(velocity_) < 0.01f) {
      value_ = target_;
      velocity_ = 0.0f;
      return false;
    }
    return true;
  }

  bool isMoving() const {
    return std::fabs(target_ - value_) >= 0.002f || std::fabs(velocity_) >= 0.01f;
  }

  float value() const { return value_; }
  float target() const { return target_; }

 private:
  float value_ = 0.0f;
  float target_ = 0.0f;
  float velocity_ = 0.0f;
  float stiffness_ = 500.0f;
  float damping_ = 38.0f;
};

class EasingValue {
 public:
  explicit EasingValue(float initial = 0.0f, uint32_t durationMs = 300)
      : from_(initial), to_(initial), value_(initial), durationMs_(durationMs) {}

  void start(float from, float to, uint32_t nowMs, uint32_t durationMs = 300) {
    from_ = from;
    to_ = to;
    value_ = from;
    startMs_ = nowMs;
    durationMs_ = std::max<uint32_t>(1, durationMs);
    active_ = (from != to);
  }

  void snap(float v) {
    from_ = to_ = value_ = v;
    active_ = false;
  }

  void update(uint32_t nowMs) {
    if (!active_) return;
    const uint32_t elapsed = nowMs - startMs_;
    if (elapsed >= durationMs_) {
      value_ = to_;
      active_ = false;
      return;
    }
    const float t = static_cast<float>(elapsed) / static_cast<float>(durationMs_);
    const float inv = 1.0f - t;
    const float eased = 1.0f - inv * inv * inv;  // easeOutCubic
    value_ = from_ + (to_ - from_) * eased;
  }

  bool isMoving() const { return active_; }
  float value() const { return value_; }

 private:
  float from_ = 0.0f;
  float to_ = 0.0f;
  float value_ = 0.0f;
  uint32_t startMs_ = 0;
  uint32_t durationMs_ = 300;
  bool active_ = false;
};

inline uint32_t mixRgb(uint32_t a, uint32_t b, float t) {
  const float u = std::min(1.0f, std::max(0.0f, t));
  const int r0 = (a >> 16) & 0xFF, g0 = (a >> 8) & 0xFF, b0 = a & 0xFF;
  const int r1 = (b >> 16) & 0xFF, g1 = (b >> 8) & 0xFF, b1 = b & 0xFF;
  const uint32_t r = static_cast<uint32_t>(r0 + (r1 - r0) * u + 0.5f);
  const uint32_t g = static_cast<uint32_t>(g0 + (g1 - g0) * u + 0.5f);
  const uint32_t bl = static_cast<uint32_t>(b0 + (b1 - b0) * u + 0.5f);
  return (r << 16) | (g << 8) | bl;
}

inline uint32_t scaleRgb(uint32_t rgb, float factor) {
  const uint32_t r = std::min<uint32_t>(
      255, static_cast<uint32_t>(((rgb >> 16) & 0xFF) * factor + 0.5f));
  const uint32_t g = std::min<uint32_t>(
      255, static_cast<uint32_t>(((rgb >> 8) & 0xFF) * factor + 0.5f));
  const uint32_t b = std::min<uint32_t>(
      255, static_cast<uint32_t>((rgb & 0xFF) * factor + 0.5f));
  return (r << 16) | (g << 8) | b;
}

}  // namespace liangzi
