// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zyron::analysis {

enum class TempoPriorMode {
  None,         // 60 .. 200 BPM default
  DnB,          // 160 .. 180 BPM prior (§15, ADR-0010)
  HouseTechno,  // 120 .. 132 BPM
  HipHop        // 80 .. 115 BPM
};

struct BeatDetectionResult {
  double bpm{0.0};
  std::int64_t firstBeatFrame{0};            // sample frame in original sample rate
  std::vector<std::int64_t> beatFrames;      // all beat positions in original sample rate
  std::vector<std::int64_t> downbeatFrames;  // bar starts (every 4th beat in 4/4)
  std::string gridJson;                      // serialized grid metadata
  bool success{false};
};

/// High-accuracy beat tracking, tempo estimation, and beatgrid construction (SPEC sections 15, 16, ADR-0010).
/// Integrates 22.05 kHz mel spectral novelty, comb autocorrelation, peak-picking, and DnB tempo priors.
class BeatDetector {
 public:
  BeatDetector() = default;
  ~BeatDetector() = default;

  /// Resolves octave (half-time / double-time) ambiguity based on genre tempo prior.
  [[nodiscard]] static double resolveTempo(double rawBpm, TempoPriorMode prior) noexcept;

  /// Peak picking on 50 FPS activation logits (Beat This! contract: ±3 frames, > threshold).
  [[nodiscard]] static std::vector<std::size_t> pickPeaks(const float* logits,
                                                          std::size_t numFrames,
                                                          float threshold = 0.0f,
                                                          int radius = 3);

  /// Builds a BeatDetectionResult from detected beat frame indices at 50 FPS.
  [[nodiscard]] static BeatDetectionResult buildGridFromBeats(
      const std::vector<std::size_t>& beatFrames50Fps,
      const std::vector<std::size_t>& downbeatFrames50Fps,
      int originalSampleRate,
      std::size_t originalNumSamples,
      TempoPriorMode prior = TempoPriorMode::DnB);

  /// Analyzes audio signal to detect BPM and beatgrid.
  [[nodiscard]] BeatDetectionResult detect(const float* audio,
                                           std::size_t numSamples,
                                           int sampleRate,
                                           TempoPriorMode prior = TempoPriorMode::DnB) const;
};

}  // namespace zyron::analysis
