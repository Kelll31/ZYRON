// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace zyron::audio {

/// Low-latency lookahead brickwall master limiter (SPEC section 20, SPEC gap #11).
///
/// Features:
///  - Lookahead delay buffer (up to 256 samples, ~5 ms @ 48 kHz; default 1.0 ms)
///  - True lookahead: the gain needed for a peak is the minimum over the lookahead window, smoothed by a moving average
///    of the same length, so the gain is already down when the peak leaves the delay line (no overs, and no clipping
///    of the waveform: the safety clamp stays at rounding-error level)
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

  /// Delay the signal gets (samples) whether the limiter acts or not; delay anything that has to stay aligned with it.
  [[nodiscard]] int latencySamples() const noexcept { return delaySamples_; }

  /// Returns recent gain reduction (linear, <= 1.0) for telemetry/metering.
  [[nodiscard]] float currentGainReduction() const noexcept;

 private:
  void updateCoefficients() noexcept;

  double sampleRate_{48000.0};
  std::atomic<bool> enabled_{true};
  std::atomic<float> ceilingDb_{kDefaultCeilingDb};
  std::atomic<float> releaseMs_{kDefaultReleaseMs};
  std::atomic<float> lookaheadMs_{kDefaultLookaheadMs};

  void resetGainHistory() noexcept;

  static constexpr std::size_t kHistorySize = 512;  // power of two >= kMaxDelaySamples + 1
  static constexpr std::size_t kHistoryMask = kHistorySize - 1;

  // DSP cached parameters
  float ceilingLinear_{0.966F};
  float releaseCoeff_{0.0004F};
  int delaySamples_{48};
  int historyWindow_{49};  // delaySamples_ + 1 the history was built for

  // State
  float currentGain_{1.0F};
  std::size_t writePos_{0};
  std::array<float, kMaxDelaySamples> delayL_{};
  std::array<float, kMaxDelaySamples> delayR_{};

  // Sliding minimum of the required gain (monotonic queue) and its moving average
  std::int64_t sampleCounter_{0};
  std::array<std::int64_t, kHistorySize> queueIndex_{};
  std::array<float, kHistorySize> queueValue_{};
  std::size_t queueHead_{0};
  std::size_t queueCount_{0};
  std::array<float, kHistorySize> minHistory_{};
  double minHistorySum_{0.0};
};

}  // namespace zyron::audio
