// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace zyron::analysis {

/// Constant-Q Transform (CQT) for musical harmony and chord recognition (ROADMAP P3-13, docs/AI_MODELS.md).
/// Feeds ChordMini (144 bins, 24 bins/octave, 22.05 kHz).
class ConstantQTransform {
 public:
  struct Config {
    int sampleRate{22050};
    float fMin{32.703f};         // C1 (approx 32.7 Hz)
    std::size_t numBins{144};    // 6 octaves * 24 bins/octave
    std::size_t binsPerOctave{24};
    std::size_t hopLength{2048};
  };

  explicit ConstantQTransform(Config config = {});

  [[nodiscard]] const Config& config() const noexcept { return config_; }
  [[nodiscard]] std::size_t numBins() const noexcept { return config_.numBins; }
  [[nodiscard]] float centerFrequency(std::size_t bin) const noexcept;

  /// Computes the magnitude CQT spectrogram for audio.
  /// Output shape is [numFrames][numBins].
  [[nodiscard]] std::vector<std::vector<float>> processMagnitude(
      const float* audio, std::size_t numSamples) const;

 private:
  struct Kernel {
    std::vector<std::complex<float>> temporal;
    float normFactor{1.0f};
  };

  void initKernels();

  Config config_;
  float q_{1.0f};
  std::vector<Kernel> kernels_;
};

}  // namespace zyron::analysis
