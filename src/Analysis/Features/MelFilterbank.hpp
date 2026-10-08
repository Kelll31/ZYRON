// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

#include "Analysis/Features/Stft.hpp"

namespace zyron::analysis {

/// Slaney-style triangular mel-filterbank matrix and mel-spectrogram extractor
/// (SPEC section 15, ROADMAP P3-13, docs/AI_MODELS.md for Beat This! contract).
class MelFilterbank {
 public:
  struct Config {
    int sampleRate{22050};
    std::size_t nFft{1024};
    std::size_t hopLength{441};
    std::size_t nMels{128};
    float fMin{0.0f};
    float fMax{11025.0f};  // sampleRate / 2
    bool normSlaney{true}; // Slaney area normalization
    bool log1p1000{true};  // log1p(1000 * x) per Beat This! contract
  };

  explicit MelFilterbank(Config config = {});

  [[nodiscard]] const Config& config() const noexcept { return config_; }
  [[nodiscard]] std::size_t nMels() const noexcept { return config_.nMels; }
  [[nodiscard]] std::size_t numBins() const noexcept { return config_.nFft / 2 + 1; }

  /// Returns the precomputed filterbank weights matrix [nMels][numBins].
  [[nodiscard]] const std::vector<std::vector<float>>& filterbank() const noexcept {
    return filterbank_;
  }

  /// Converts FFT linear bins to mel frequency bins for a single frame.
  void applyToFrame(const float* powerSpectrum, float* melOut) const;

  /// Computes the complete mel-spectrogram for 22.05 kHz audio.
  /// Output shape is [numFrames][nMels].
  [[nodiscard]] std::vector<std::vector<float>> computeMelSpectrogram(
      const float* audio, std::size_t numSamples) const;

  /// Converts frequency in Hz to mel scale (Slaney Auditory Toolbox convention).
  [[nodiscard]] static float hzToMel(float hz) noexcept;

  /// Converts mel scale to frequency in Hz (Slaney Auditory Toolbox convention).
  [[nodiscard]] static float melToHz(float mel) noexcept;

 private:
  void initFilterbank();

  Config config_;
  Stft stft_;
  std::vector<std::vector<float>> filterbank_;  // [nMels][numBins]
};

}  // namespace zyron::analysis
