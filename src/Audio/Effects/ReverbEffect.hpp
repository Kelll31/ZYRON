// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "Audio/Effects/Effect.hpp"

namespace zyron::audio {

/// Freeverb-style stereo algorithmic reverb with comb filters and all-pass diffusers.
/// Realtime safe, click-free parameter smoothing.
class ReverbEffect : public Effect {
 public:
  ReverbEffect();
  ~ReverbEffect() override = default;

  [[nodiscard]] std::string_view id() const noexcept override { return "reverb"; }
  [[nodiscard]] std::string_view name() const noexcept override { return "Reverb"; }

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
  void setRoomSize(float size) noexcept;
  [[nodiscard]] float roomSize() const noexcept;

  void setDamping(float damp) noexcept;
  [[nodiscard]] float damping() const noexcept;

  void setStereoWidth(float width) noexcept;
  [[nodiscard]] float stereoWidth() const noexcept;

 private:
  struct CombFilter {
    std::vector<float> buffer;
    int bufferSize{0};
    int bufferIndex{0};
    float filterStore{0.0F};

    void init(int size);
    void reset() noexcept;
    float process(float input, float feedback, float damp) noexcept;
  };

  struct AllpassFilter {
    std::vector<float> buffer;
    int bufferSize{0};
    int bufferIndex{0};

    void init(int size);
    void reset() noexcept;
    float process(float input) noexcept;
  };

  static constexpr std::size_t kNumCombs = 8;
  static constexpr std::size_t kNumAllpasses = 4;
  static constexpr std::size_t kNumParams = 3;
  static const std::array<ParameterDescriptor, kNumParams> kDescriptors;

  double sampleRate_{48000.0};

  std::array<CombFilter, kNumCombs> combsL_;
  std::array<CombFilter, kNumCombs> combsR_;
  std::array<AllpassFilter, kNumAllpasses> allpassesL_;
  std::array<AllpassFilter, kNumAllpasses> allpassesR_;

  std::atomic<bool> enabled_{true};
  std::atomic<float> targetDryWet_{0.3F};
  std::atomic<float> targetRoomSize_{0.75F};
  std::atomic<float> targetDamping_{0.25F};
  std::atomic<float> targetWidth_{1.0F};

  float currentDryWet_{0.3F};
  float currentRoomSize_{0.75F};
  float currentDamping_{0.25F};
  float currentWidth_{1.0F};
  float smoothCoeff_{0.005F};
};

}  // namespace zyron::audio
