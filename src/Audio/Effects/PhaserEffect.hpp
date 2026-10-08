// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>

#include "Audio/Effects/Effect.hpp"

namespace zyron::audio {

/// 6-Stage all-pass phaser effect with LFO modulation and resonant feedback.
/// Realtime safe, click-free parameter smoothing.
class PhaserEffect : public Effect {
 public:
  PhaserEffect();
  ~PhaserEffect() override = default;

  [[nodiscard]] std::string_view id() const noexcept override { return "phaser"; }
  [[nodiscard]] std::string_view name() const noexcept override { return "Phaser"; }

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
  void setRateHz(float rate) noexcept;
  [[nodiscard]] float rateHz() const noexcept;

  void setDepth(float depth) noexcept;
  [[nodiscard]] float depth() const noexcept;

  void setFeedback(float feedback) noexcept;
  [[nodiscard]] float feedback() const noexcept;

  void setBaseFrequencyHz(float freqHz) noexcept;
  [[nodiscard]] float baseFrequencyHz() const noexcept;

 private:
  static constexpr std::size_t kNumStages = 6;
  static constexpr std::size_t kNumParams = 4;
  static const std::array<ParameterDescriptor, kNumParams> kDescriptors;

  double sampleRate_{48000.0};
  float lfoPhase_{0.0F};

  // State for allpass stages
  std::array<float, kNumStages> stagesL_{};
  std::array<float, kNumStages> stagesR_{};
  float lastOutputL_{0.0F};
  float lastOutputR_{0.0F};

  std::atomic<bool> enabled_{true};
  std::atomic<float> targetDryWet_{0.5F};
  std::atomic<float> targetRateHz_{0.3F};
  std::atomic<float> targetDepth_{0.8F};
  std::atomic<float> targetFeedback_{0.5F};
  std::atomic<float> targetBaseFreqHz_{1000.0F};

  float currentDryWet_{0.5F};
  float currentDepth_{0.8F};
  float currentFeedback_{0.5F};
  float currentBaseFreqHz_{1000.0F};
  float smoothCoeff_{0.005F};
};

}  // namespace zyron::audio
