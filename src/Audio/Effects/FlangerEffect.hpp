// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "Audio/Effects/Effect.hpp"

namespace zyron::audio {

/// Classic DJ Flanger effect with LFO-modulated delay line, bipolar feedback, and stereo phase offset.
/// Realtime safe, click-free parameter smoothing.
class FlangerEffect : public Effect {
 public:
  FlangerEffect();
  ~FlangerEffect() override = default;

  [[nodiscard]] std::string_view id() const noexcept override { return "flanger"; }
  [[nodiscard]] std::string_view name() const noexcept override { return "Flanger"; }

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

  void setStereoPhaseDeg(float phaseDeg) noexcept;
  [[nodiscard]] float stereoPhaseDeg() const noexcept;

 private:
  static constexpr std::size_t kNumParams = 4;
  static const std::array<ParameterDescriptor, kNumParams> kDescriptors;

  double sampleRate_{48000.0};
  int bufferMask_{0};
  std::vector<float> bufferL_;
  std::vector<float> bufferR_;
  int writeIndex_{0};

  float lfoPhase_{0.0F};

  std::atomic<bool> enabled_{true};
  std::atomic<float> targetDryWet_{0.5F};
  std::atomic<float> targetRateHz_{0.25F};
  std::atomic<float> targetDepth_{0.7F};
  std::atomic<float> targetFeedback_{0.5F};
  std::atomic<float> targetStereoPhaseDeg_{90.0F};

  float currentDryWet_{0.5F};
  float currentDepth_{0.7F};
  float currentFeedback_{0.5F};
  float smoothCoeff_{0.005F};
};

}  // namespace zyron::audio
