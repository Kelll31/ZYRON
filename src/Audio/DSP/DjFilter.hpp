// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>

#include "Audio/DSP/StateVariableFilter.hpp"

namespace zyron::audio {

/// Bipolar DJ HPF/LPF Filter with resonance (SPEC section 22).
///
/// Knob parameter in [-1.0, +1.0]:
///   - 0.0 : Neutral / Bypassed
///   - < 0.0 : Low-Pass Filter (sweeps 20 kHz down to 20 Hz)
///   - > 0.0 : High-Pass Filter (sweeps 20 Hz up to 20 kHz)
///
/// Resonance Q: [0.707, 4.0], default 1.0.
/// Parameters are smoothed with a 10 ms time constant. Realtime safe.
class DjFilter {
 public:
  DjFilter();
  ~DjFilter() = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Sets bipolar knob position [-1.0 .. +1.0].
  void setFilter(float knobPosition) noexcept;

  /// Sets resonance Q factor [0.5 .. 5.0].
  void setResonance(float q) noexcept;

  /// Processes stereo audio in place. Realtime safe.
  void process(float* const* channels, int numChannels, int numSamples) noexcept;

 private:
  double sampleRate_{48000.0};
  float rampCoeff_{0.002F};

  std::atomic<float> targetKnob_{0.0F};
  std::atomic<float> targetQ_{1.0F};

  float currentKnob_{0.0F};
  float currentQ_{1.0F};

  std::array<StateVariableFilter, 2> svf_;
};

}  // namespace zyron::audio
