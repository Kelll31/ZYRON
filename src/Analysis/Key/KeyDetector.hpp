// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

#include "Analysis/Key/MusicalKey.hpp"

namespace zyron::analysis {

/// Musical key detector utilizing harmonic pitch chromagram and Krumhansl-Schmuckler profiles,
/// prepared for S-KEY ONNX inference (SPEC section 53, ADR-0010, docs/AI_MODELS.md).
class KeyDetector {
 public:
  KeyDetector() = default;
  ~KeyDetector() = default;

  /// Detects the global musical key and maps it to standard name and Camelot notation.
  [[nodiscard]] MusicalKey detectKey(const float* audio,
                                     std::size_t numSamples,
                                     int sampleRate) const;

  /// Computes a 12-dimensional pitch-class profile (chromagram) normalized across the audio signal.
  [[nodiscard]] std::vector<float> computeChromaProfile(const float* audio,
                                                        std::size_t numSamples,
                                                        int sampleRate) const;
};

}  // namespace zyron::analysis
