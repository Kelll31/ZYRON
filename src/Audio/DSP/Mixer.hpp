// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Audio/DSP/MasterLimiter.hpp"
#include "Core/Audio/MixerTypes.hpp"

namespace zyron::audio {

using core::CrossfaderCurve;
using core::CrossfaderAssign;

/// 2-to-4 Channel DJ Mixer with crossfader, master gain, cue bus, and brickwall limiter (SPEC section 20).
///
/// Features:
///  - 2 channels (MVP1), expandable to 4 channels (Phase 4)
///  - Crossfader curves: Linear, ConstantPower (default), Cut
///  - Smooth 5 ms crossfader ramping and 10 ms master gain ramping
///  - Master limiter with lookahead and brickwall ceiling
///  - Realtime safe (zero allocations in audio render)
class Mixer {
 public:
  static constexpr int kMaxChannels = 4;
  static constexpr float kMinGainDb = -60.0F;
  static constexpr float kMaxGainDb = 12.0F;

  Mixer();
  ~Mixer() = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  // Crossfader control
  void setCrossfader(float position) noexcept;  // [-1.0 (Deck A) to +1.0 (Deck B)]
  [[nodiscard]] float crossfader() const noexcept;

  void setCrossfaderCurve(CrossfaderCurve curve) noexcept;
  [[nodiscard]] CrossfaderCurve crossfaderCurve() const noexcept;

  void setChannelAssign(int channel, CrossfaderAssign assign) noexcept;
  [[nodiscard]] CrossfaderAssign channelAssign(int channel) const noexcept;

  // Master Gain control
  void setMasterGainDb(float gainDb) noexcept;
  [[nodiscard]] float masterGainDb() const noexcept;

  // Master Limiter access
  [[nodiscard]] MasterLimiter& masterLimiter() noexcept { return masterLimiter_; }
  [[nodiscard]] const MasterLimiter& masterLimiter() const noexcept { return masterLimiter_; }

  // Cue Bus
  void setCue(int channel, bool enabled) noexcept;
  [[nodiscard]] bool isCue(int channel) const noexcept;

  // Telemetry
  [[nodiscard]] float masterPeakLeft() const noexcept;
  [[nodiscard]] float masterPeakRight() const noexcept;

  /// Convenience 2-deck stereo process. Realtime safe.
  void process(const float* leftA, const float* rightA, const float* leftB, const float* rightB, float* masterLeft,
               float* masterRight, int numSamples) noexcept;

  /// Multi-channel stereo process (up to kMaxChannels). Realtime safe.
  void process(const float* const* channelLefts, const float* const* channelRights, int numChannels, float* masterLeft,
               float* masterRight, int numSamples) noexcept;

  /// Renders cue bus (for headphones) by summing active cue channels. Realtime safe.
  void processCue(const float* const* channelLefts, const float* const* channelRights, int numChannels, float* cueLeft,
                  float* cueRight, int numSamples) noexcept;

 private:
  void computeCrossfaderGains(float position, float& gainLeft, float& gainRight) const noexcept;

  double sampleRate_{48000.0};
  float rampCoeffCrossfader_{0.004F};
  float rampCoeffGain_{0.002F};

  // Crossfader state
  std::atomic<float> targetCrossfader_{0.0F};
  float currentCrossfader_{0.0F};
  std::atomic<CrossfaderCurve> curve_{CrossfaderCurve::ConstantPower};
  std::array<std::atomic<CrossfaderAssign>, kMaxChannels> channelAssign_{};

  // Master gain
  std::atomic<float> targetMasterGainLinear_{1.0F};
  float currentMasterGainLinear_{1.0F};

  // Cue state
  std::array<std::atomic<bool>, kMaxChannels> cueEnabled_{};

  // Telemetry
  std::atomic<float> masterPeakLeft_{0.0F};
  std::atomic<float> masterPeakRight_{0.0F};

  MasterLimiter masterLimiter_;
};

}  // namespace zyron::audio
