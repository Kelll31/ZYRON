// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "Audio/Effects/Effect.hpp"

namespace zyron::audio {

/// Digital stereo delay effect with high-frequency damping, ping-pong mode, and soft-clipped feedback.
/// Realtime-safe, zero allocations on audio thread, click-free parameter smoothing.
class DelayEffect : public Effect {
 public:
  DelayEffect();
  ~DelayEffect() override = default;

  [[nodiscard]] std::string_view id() const noexcept override { return "delay"; }
  [[nodiscard]] std::string_view name() const noexcept override { return "Delay"; }

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
  void setDelayTimeMs(float ms) noexcept;
  [[nodiscard]] float delayTimeMs() const noexcept;

  void setFeedback(float feedback) noexcept;
  [[nodiscard]] float feedback() const noexcept;

  void setDampingHz(float cutoffHz) noexcept;
  [[nodiscard]] float dampingHz() const noexcept;

  void setPingPong(bool enabled) noexcept;
  [[nodiscard]] bool isPingPong() const noexcept;

 private:
  static constexpr std::size_t kNumParams = 4;
  static const std::array<ParameterDescriptor, kNumParams> kDescriptors;

  double sampleRate_{48000.0};
  int bufferMask_{0};
  std::vector<float> bufferL_;
  std::vector<float> bufferR_;
  int writeIndex_{0};

  // State
  float dampStateL_{0.0F};
  float dampStateR_{0.0F};

  // Atomic targets
  std::atomic<bool> enabled_{true};
  std::atomic<float> targetDryWet_{0.3F};
  std::atomic<float> targetTimeMs_{375.0F};
  std::atomic<float> targetFeedback_{0.4F};
  std::atomic<float> targetDampingHz_{8000.0F};
  std::atomic<bool> pingPong_{false};

  // Smoothed runtime values
  float currentDryWet_{0.3F};
  float currentDelaySamples_{18000.0F};
  float currentFeedback_{0.4F};
  float currentDampCoeff_{0.2F};
  float smoothCoeff_{0.005F};
};

}  // namespace zyron::audio
