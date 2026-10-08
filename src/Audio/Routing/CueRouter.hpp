// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>

namespace zyron::audio {

/// Headphone and master bus routing modes (SPEC section 45, SPEC gap #4).
enum class HeadphoneRoutingMode : std::uint8_t {
  Disabled,      // Master to channels 0,1; channels >= 2 silent
  MultiChannel,  // Master to 0,1; Headphone Cue to 2,3 (requires >= 4 hardware channels)
  SplitCue       // Left = Cue mono, Right = Master mono (for 2-channel soundcard headphone monitoring)
};

/// Handles headphone cue and master bus routing with parameter smoothing and split-cue fallback.
class CueRouter {
 public:
  CueRouter();
  ~CueRouter() = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  void setMode(HeadphoneRoutingMode mode) noexcept;
  [[nodiscard]] HeadphoneRoutingMode mode() const noexcept;

  void setHeadphoneVolume(float volume) noexcept;  // 0.0 to 1.0 linear
  [[nodiscard]] float headphoneVolume() const noexcept;

  void setHeadphoneMix(float mix) noexcept;  // 0.0 (100% Cue) to 1.0 (100% Master)
  [[nodiscard]] float headphoneMix() const noexcept;

  /// Routes stereo master and stereo cue buses to hardware output channels. Realtime safe.
  void route(const float* masterL, const float* masterR, const float* cueL, const float* cueR, float* const* outputs,
             int numOutputChannels, int numSamples) noexcept;

 private:
  double sampleRate_{48000.0};
  float rampCoeff_{0.002F};

  std::atomic<HeadphoneRoutingMode> mode_{HeadphoneRoutingMode::Disabled};
  std::atomic<float> targetVolume_{1.0F};
  float currentVolume_{1.0F};

  std::atomic<float> targetMix_{0.0F};  // 0.0 = pure Cue
  float currentMix_{0.0F};
};

}  // namespace zyron::audio
