// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <cmath>

namespace zyron::audio {

/// Topology-Preserving Transform (TPT) State-Variable Filter (SVF).
/// Unconditionally stable under fast parameter sweeps, click-free, and real-time safe.
class StateVariableFilter {
 public:
  StateVariableFilter() = default;

  void prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
    reset();
  }

  void reset() noexcept {
    s1_ = 0.0F;
    s2_ = 0.0F;
  }

  /// Sets cutoff frequency in Hz and resonance Q factor (Q >= 0.5, default Butterworth Q = 0.707).
  void setParameters(float cutoffHz, float q = 0.7071F) noexcept {
    cutoffHz = std::clamp(cutoffHz, 10.0F, static_cast<float>(sampleRate_ * 0.49));
    q = std::max(0.1F, q);

    constexpr float kPi = 3.14159265358979323846F;
    const float g = std::tan(kPi * cutoffHz / static_cast<float>(sampleRate_));
    const float k = 1.0F / q;

    a1_ = 1.0F / (1.0F + g * (g + k));
    a2_ = g * a1_;
    a3_ = g * a2_;
    k_ = k;
  }

  /// Evaluates one sample and returns lowpass, bandpass, highpass, and allpass outputs.
  struct FilterOutput {
    float lowpass{0.0F};
    float bandpass{0.0F};
    float highpass{0.0F};
    float allpass{0.0F};
  };

  [[nodiscard]] FilterOutput process(float x) noexcept {
    // TPT SVF equations
    const float v3 = x - s2_;
    const float v1 = a1_ * s1_ + a2_ * v3;
    const float v2 = s2_ + a2_ * s1_ + a3_ * v3;

    // State update
    s1_ = 2.0F * v1 - s1_;
    s2_ = 2.0F * v2 - s2_;

    // Anti-denormal flush
    if (std::abs(s1_) < 1e-15F) {
      s1_ = 0.0F;
    }
    if (std::abs(s2_) < 1e-15F) {
      s2_ = 0.0F;
    }

    const float lp = v2;
    const float bp = v1;
    const float hp = x - k_ * v1 - v2;
    const float ap = x - 2.0F * k_ * v1;

    return {lp, bp, hp, ap};
  }

 private:
  double sampleRate_{48000.0};
  float s1_{0.0F};
  float s2_{0.0F};

  float a1_{0.0F};
  float a2_{0.0F};
  float a3_{0.0F};
  float k_{1.4142F};
};

}  // namespace zyron::audio
