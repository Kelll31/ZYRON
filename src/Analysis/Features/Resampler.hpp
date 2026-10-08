// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

namespace zyron::analysis {

/// Band-limited windowed-sinc audio resampler for AI feature extraction pipelines
/// (SPEC section 15, ROADMAP P3-13, docs/AI_MODELS.md).
/// Converts between audio rates (44.1/48/96 kHz) and model input rates (22.05/24/44.1 kHz).
class AudioResampler {
 public:
  AudioResampler(int sourceRate, int targetRate, int filterRadius = 16);

  [[nodiscard]] int sourceRate() const noexcept { return sourceRate_; }
  [[nodiscard]] int targetRate() const noexcept { return targetRate_; }

  /// Resamples a 1D audio buffer.
  [[nodiscard]] std::vector<float> process(const float* input, std::size_t numSamples) const;

  /// Convenience one-shot resampling function.
  [[nodiscard]] static std::vector<float> resample(const float* input,
                                                   std::size_t numSamples,
                                                   int inRate,
                                                   int outRate);

 private:
  int sourceRate_{44100};
  int targetRate_{22050};
  int filterRadius_{16};  // half-length of sinc kernel
};

}  // namespace zyron::analysis
