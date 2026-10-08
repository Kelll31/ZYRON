// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>

#include "Audio/DSP/StateVariableFilter.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// 3-band DJ Isolator/EQ with parameter smoothing and full kill capability (SPEC section 21).
///
/// Crossovers:
///  - Low / Mid: 250 Hz
///  - Mid / High: 2500 Hz
///
/// Gain range: -60 dB (kill floor) to +12 dB boost. Below -59 dB, gain is set to 0.0 (silent).
/// Parameter changes are smoothed with a 10 ms time constant to guarantee click-free operation.
class ThreeBandEq {
 public:
  static constexpr float kLowCrossoverHz = 250.0F;
  static constexpr float kHighCrossoverHz = 2500.0F;

  ThreeBandEq();
  ~ThreeBandEq() = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  void setBandDb(core::EqBand band, float gainDb) noexcept;
  void setLowDb(float gainDb) noexcept;
  void setMidDb(float gainDb) noexcept;
  void setHighDb(float gainDb) noexcept;

  /// Processes stereo or multi-channel audio in place. Realtime safe.
  void process(float* const* channels, int numChannels, int numSamples) noexcept;

 private:
  double sampleRate_{48000.0};
  float rampCoeff_{0.002F};

  // LR4 crossover filter stages per channel (2 channels: left, right)
  // Low crossover at 250 Hz
  std::array<StateVariableFilter, 2> svfLowA_;
  std::array<StateVariableFilter, 2> svfLowB_LP_;
  std::array<StateVariableFilter, 2> svfLowB_HP_;

  // High crossover at 2500 Hz
  std::array<StateVariableFilter, 2> svfHighA_;
  std::array<StateVariableFilter, 2> svfHighB_LP_;
  std::array<StateVariableFilter, 2> svfHighB_HP_;

  // Allpass compensation for Low band at 2500 Hz
  std::array<StateVariableFilter, 2> svfHighAP_;

  // Target and current linear gains for Low, Mid, High
  std::array<std::atomic<float>, 3> targetGainLinear_;
  std::array<float, 3> currentGainLinear_{1.0F, 1.0F, 1.0F};
};

}  // namespace zyron::audio
