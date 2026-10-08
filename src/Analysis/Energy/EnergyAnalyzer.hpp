// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

namespace zyron::analysis {

/// Breakdown and overall score of track energy (SPEC sections 53, 56).
struct EnergyResult {
  float globalEnergy{1.0f};          // Overall track energy rating: 1.0 .. 10.0 (clamped)
  std::vector<float> energyCurve;    // Energy trajectory over time (each value 1.0 .. 10.0)
  float curveStepSec{0.5f};          // Interval between energy curve points in seconds (e.g. 0.5s = 2 Hz)
  float loudnessScore{0.0f};         // Normalized RMS loudness factor: 0.0 .. 1.0
  float spectralScore{0.0f};         // Normalized spectral brightness / centroid: 0.0 .. 1.0
  float transientScore{0.0f};        // Normalized rhythmic transient density: 0.0 .. 1.0
  float bassScore{0.0f};             // Normalized low-frequency / sub-bass ratio: 0.0 .. 1.0
};

/// Track energy analyzer (SPEC section 53, ADR-0010).
/// Computes multi-factor DJ track energy rating on a 1.0 .. 10.0 scale,
/// combining RMS loudness, spectral centroid, transient density, and bass weight.
class EnergyAnalyzer {
 public:
  EnergyAnalyzer() = default;
  ~EnergyAnalyzer() = default;

  /// Analyzes complete mono audio signal, returning global energy rating and time-series curve.
  [[nodiscard]] EnergyResult analyze(const float* audio,
                                     std::size_t numSamples,
                                     int sampleRate,
                                     float stepSec = 0.5f) const;

  /// Computes energy score for a short audio frame / window in range 1.0 .. 10.0.
  [[nodiscard]] float computeWindowEnergy(const float* audio,
                                          std::size_t numSamples,
                                          int sampleRate) const;
};

}  // namespace zyron::analysis
