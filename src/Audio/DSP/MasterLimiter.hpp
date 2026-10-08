// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace zyron::audio {

/// Low-latency lookahead brickwall master limiter (SPEC section 20, SPEC gap #11).
///
/// Features:
///  - Lookahead delay buffer (up to 256 samples, ~5 ms @ 48 kHz; default 1.0 ms)
///  - Transparent peak detection and smooth gain reduction
///  - Configurable ceiling (-12 dBFS to 0 dBFS; default -0.3 dBFS)
///  - Exponential release envelope (5 ms to 500 ms; default 50 ms)
///  - Guaranteed ceiling limit (zero digital clipping / overs)
///  - Realtime safe (zero allocations, lock-free)
class MasterLimiter {
 public:
  static constexpr float kMinCeilingDb = -12.0F;
  static constexpr float kMaxCeilingDb = 0.0F;
  static constexpr float kDefaultCeilingDb = -0.3F;
  static constexpr float kDefaultReleaseMs = 50.0F;
  static constexpr float kDefaultLookaheadMs = 1.0F;
  static constexpr std::size_t kMaxDelaySamples = 256;

  MasterLimiter();
  ~MasterLimiter() = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  void setEnabled(bool enabled) noexcept;
  [[nodiscard]] bool isEnabled() const noexcept;

  void setCeilingDb(float ceilingDb) noexcept;
  [[nodiscard]] float ceilingDb() const noexcept;

  void setReleaseMs(float releaseMs) noexcept;
  [[nodiscard]] float releaseMs() const noexcept;

  void setLookaheadMs(float lookaheadMs) noexcept;
  [[nodiscard]] float lookaheadMs() const noexcept;

  /// Process stereo buffer in place. Realtime safe.
  void process(float* left, float* right, int numSamples) noexcept;

  /// Returns recent gain reduction (linear, <= 1.0) for telemetry/metering.
  [[nodiscard]] float currentGainReduction() const noexcept;

 private:
  void updateCoefficients() noexcept;

  double sampleRate_{48000.0};
  std::atomic<bool> enabled_{true};
  std::atomic<float> ceilingDb_{kDefaultCeilingDb};
  std::atomic<float> releaseMs_{kDefaultReleaseMs};
  std::atomic<float> lookaheadMs_{kDefaultLookaheadMs};

  // DSP cached parameters
  float ceilingLinear_{0.966F};
  float attackCoeff_{0.02F};
  float releaseCoeff_{0.0004F};
  int delaySamples_{48};

  // State
  float currentGain_{1.0F};
  std::size_t writePos_{0};
  std::array<float, kMaxDelaySamples> delayL_{};
  std::array<float, kMaxDelaySamples> delayR_{};
};

}  // namespace zyron::audio
