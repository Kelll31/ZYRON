// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Analysis/Bpm/Beatgrid.hpp"
#include "Analysis/Energy/EnergyAnalyzer.hpp"
#include "Analysis/Structure/StructureTypes.hpp"

namespace zyron::analysis {

/// DJ track structural analyzer (SPEC sections 54, 55, 57, ROADMAP P6-01, P6-03).
/// Detects Intro, Build, Drop, Break, Drop 2, Outro segments and mix-point cues.
class StructureSegmenter {
 public:
  StructureSegmenter() = default;
  ~StructureSegmenter() = default;

  /// Performs structural segmentation on audio signal.
  /// \param audio Pointer to mono audio buffer.
  /// \param numSamples Total number of samples.
  /// \param sampleRate Audio sampling rate.
  /// \param beatgrid Optional beatgrid information for bar-quantized phrase boundaries.
  /// \param energy Optional precomputed energy analysis (computed internally if null).
  [[nodiscard]] TrackStructure analyze(
      const float* audio,
      std::size_t numSamples,
      int sampleRate,
      const BeatgridData* beatgrid = nullptr,
      const EnergyResult* energy = nullptr) const;

  /// Generates mix-point cues from detected track structure for hot cues and transitions (ROADMAP P6-03).
  [[nodiscard]] std::vector<MixPointCue> generateMixPointCues(
      const TrackStructure& structure) const;
};

}  // namespace zyron::analysis
