// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

namespace zyron::analysis {

/// Breakdown and overall score of track energy (SPEC sections 53, 56, ROADMAP P6-02).
struct EnergyResult {
  float globalEnergy{1.0f};          // Overall track energy rating: 1.0 .. 10.0 (clamped)
  std::vector<float> energyCurve;    // Energy trajectory over time (each value 1.0 .. 10.0)
  float curveStepSec{0.5f};          // Interval between energy curve points in seconds (e.g. 0.5s = 2 Hz)
  float loudnessScore{0.0f};         // Normalized RMS loudness factor: 0.0 .. 1.0
  float spectralScore{0.0f};         // Normalized spectral brightness / centroid: 0.0 .. 1.0
  float transientScore{0.0f};        // Normalized rhythmic transient density: 0.0 .. 1.0
  float bassScore{0.0f};             // Normalized low-frequency / sub-bass ratio: 0.0 .. 1.0

  // Energy v2 breakdown factors (SPEC section 53, ROADMAP P6-02):
  float drumDensity{0.0f};           // Normalized drum stem / percussive density: 0.0 .. 1.0
  float bassIntensity{0.0f};         // Normalized bass stem / sub-bass intensity: 0.0 .. 1.0
  float vocalDensity{0.0f};          // Normalized vocal stem / presence factor: 0.0 .. 1.0
  float dropIntensity{0.0f};         // Dynamic contrast ratio between build/break and drop: 0.0 .. 1.0
  bool hasStemAnalysis{false};       // True if separated stems were provided
};

/// Buffers for separated stems (drums, bass, vocals, other) used for high-fidelity Energy v2 analysis.
struct StemBuffers {
  const float* drums{nullptr};
  const float* bass{nullptr};
  const float* vocals{nullptr};
  const float* other{nullptr};
  std::size_t numSamples{0};
  int sampleRate{44100};
};

/// Track energy analyzer (SPEC section 53, ADR-0010, ROADMAP P6-02).
/// Computes multi-factor DJ track energy rating on a 1.0 .. 10.0 scale,
/// combining RMS loudness, spectral centroid, transient density, bass weight,
/// drum/bass/vocal density, and drop contrast intensity.
class EnergyAnalyzer {
 public:
  EnergyAnalyzer() = default;
  ~EnergyAnalyzer() = default;

  /// Analyzes complete mono audio signal, returning global energy rating and time-series curve.
  [[nodiscard]] EnergyResult analyze(const float* audio,
                                     std::size_t numSamples,
                                     int sampleRate,
                                     float stepSec = 0.5f) const;

  /// Energy v2: analyzes track using loudness, spectral density, drum/bass/vocal density, and drop intensity (§53).
  [[nodiscard]] EnergyResult analyzeV2(const float* masterAudio,
                                       std::size_t numSamples,
                                       int sampleRate,
                                       const StemBuffers* stems = nullptr,
                                       float stepSec = 0.5f) const;

  /// Computes energy score for a short audio frame / window in range 1.0 .. 10.0.
  [[nodiscard]] float computeWindowEnergy(const float* audio,
                                          std::size_t numSamples,
                                          int sampleRate) const;
};

}  // namespace zyron::analysis
