// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>

namespace zyron::audio {

/// Gentle stereo-linked bus compressor that sits in front of the master limiter ("glue").
///
/// Fixed, deliberately mild settings (no user knobs; SPEC: one decision less for the DJ):
///  - RMS detector (10 ms), threshold -9 dBFS, soft knee 6 dB, ratio 2:1;
///  - attack 30 ms, release 250 ms (both on the gain reduction in dB): kicks and snares pass before it reacts;
///  - no makeup gain.
/// Material below about -12 dBFS RMS (one track at its nominal level) is not touched at all; two loud tracks summed
/// at the crossfader centre are held together by a few dB. Realtime safe, allocation-free.
class GlueCompressor {
 public:
  static constexpr float kThresholdDb = -9.0F;
  static constexpr float kKneeDb = 6.0F;
  static constexpr float kRatio = 2.0F;
  static constexpr float kAttackMs = 30.0F;
  static constexpr float kReleaseMs = 250.0F;
  static constexpr float kDetectorMs = 10.0F;

  GlueCompressor() = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Switching off releases the gain reduction smoothly instead of cutting it.
  void setEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_relaxed); }
  [[nodiscard]] bool isEnabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

  void process(float* left, float* right, int numSamples) noexcept;

  /// Current gain reduction in dB (>= 0), for metering.
  [[nodiscard]] float gainReductionDb() const noexcept { return reductionDb_.load(std::memory_order_relaxed); }

 private:
  double sampleRate_{48000.0};
  std::atomic<bool> enabled_{true};
  std::atomic<float> reductionDb_{0.0F};

  float detectorCoeff_{0.002F};
  float attackCoeff_{0.001F};
  float releaseCoeff_{0.0001F};
  float meanSquare_{0.0F};
  float smoothedReductionDb_{0.0F};
};

}  // namespace zyron::audio
