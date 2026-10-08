// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "Audio/Effects/Effect.hpp"

namespace zyron::audio {

/// Tape-style DJ Echo effect with bandpass-filtered feedback loop and echo freeze mode.
/// Realtime safe, click-free parameter smoothing.
class EchoEffect : public Effect {
 public:
  EchoEffect();
  ~EchoEffect() override = default;

  [[nodiscard]] std::string_view id() const noexcept override { return "echo"; }
  [[nodiscard]] std::string_view name() const noexcept override { return "Echo"; }

  void prepare(double sampleRate, int maxBlockSize) noexcept override;
  void reset() noexcept override;
  void process(float* const* channels, int numChannels, int numSamples) noexcept override;

  void setEnabled(bool enabled) noexcept override;
  [[nodiscard]] bool isEnabled() const noexcept override;

  void setDryWet(float mixLinear) noexcept override;
  [[nodiscard]] float dryWet() const noexcept override;

  [[nodiscard]] std::span<const ParameterDescriptor> parameters() const noexcept override;
  void setParameter(std::size_t index, float value) noexcept override;
  [[nodiscard]] float parameter(std::size_t index) const noexcept override;

  // Specific helpers
  void setEchoTimeMs(float ms) noexcept;
  [[nodiscard]] float echoTimeMs() const noexcept;

  void setFeedback(float feedback) noexcept;
  [[nodiscard]] float feedback() const noexcept;

  void setHighpassHz(float hpHz) noexcept;
  [[nodiscard]] float highpassHz() const noexcept;

  void setLowpassHz(float lpHz) noexcept;
  [[nodiscard]] float lowpassHz() const noexcept;

  void setFreeze(bool frozen) noexcept;
  [[nodiscard]] bool isFrozen() const noexcept;

 private:
  static constexpr std::size_t kNumParams = 5;
  static const std::array<ParameterDescriptor, kNumParams> kDescriptors;

  double sampleRate_{48000.0};
  int bufferMask_{0};
  std::vector<float> bufferL_;
  std::vector<float> bufferR_;
  int writeIndex_{0};

  // 1-pole filter states
  float hpStateL_{0.0F};
  float hpStateR_{0.0F};
  float lpStateL_{0.0F};
  float lpStateR_{0.0F};

  // Targets
  std::atomic<bool> enabled_{true};
  std::atomic<float> targetDryWet_{0.35F};
  std::atomic<float> targetTimeMs_{250.0F};
  std::atomic<float> targetFeedback_{0.5F};
  std::atomic<float> targetHpHz_{200.0F};
  std::atomic<float> targetLpHz_{6000.0F};
  std::atomic<bool> frozen_{false};

  // Runtime smoothed
  float currentDryWet_{0.35F};
  float currentDelaySamples_{12000.0F};
  float currentFeedback_{0.5F};
  float currentHpCoeff_{0.02F};
  float currentLpCoeff_{0.5F};
  float smoothCoeff_{0.005F};
};

}  // namespace zyron::audio
