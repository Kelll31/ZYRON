// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "Analysis/Chord/ChordTypes.hpp"
#include "Analysis/Features/Cqt.hpp"

namespace zyron::analysis {

/// Chord & harmony analyzer integrating CQT feature extraction,
/// ChordMini ONNX 170-class inference contract, and template-matching DSP fallback (ROADMAP P6-04, docs/AI_MODELS.md).
class ChordAnalyzer {
 public:
  /// Inference callback for ChordMini neural network:
  /// Input: CQT features flat buffer [numFrames * 144]
  /// Output: Logits flat buffer [numFrames * 170]
  using InferenceCallback = std::function<std::vector<float>(
      const float* cqtFlat, std::size_t numFrames, std::size_t numBins)>;

  ChordAnalyzer();
  explicit ChordAnalyzer(InferenceCallback inferenceCallback);
  ~ChordAnalyzer() = default;

  /// Translates ChordMini 170-class index to a typed Chord.
  [[nodiscard]] static Chord classIndexToChord(int classIndex, float confidence = 1.0f);

  /// Translates a typed Chord to ChordMini 170-class index.
  [[nodiscard]] static int chordToClassIndex(const Chord& chord) noexcept;

  /// Analyzes audio to determine frame-by-frame chord progression and harmonic compatibility profile.
  [[nodiscard]] HarmonyAnalysisResult analyze(
      const float* audio, std::size_t numSamples, int sampleRate) const;

 private:
  InferenceCallback inferenceCallback_;
  ConstantQTransform cqt_;
};

}  // namespace zyron::analysis
