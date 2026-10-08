// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Features/Resampler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace zyron::analysis {

namespace {

inline float sinc(float x) noexcept {
  if (std::abs(x) < 1e-6f) {
    return 1.0f;
  }
  const float pix = std::numbers::pi_v<float> * x;
  return std::sin(pix) / pix;
}

}  // namespace

AudioResampler::AudioResampler(int sourceRate, int targetRate, int filterRadius)
    : sourceRate_(sourceRate), targetRate_(targetRate), filterRadius_(filterRadius) {
  if (sourceRate <= 0 || targetRate <= 0) {
    throw std::invalid_argument("Sample rates must be positive");
  }
  if (filterRadius <= 2) {
    filterRadius_ = 16;
  }
}

std::vector<float> AudioResampler::process(const float* input, std::size_t numSamples) const {
  if (input == nullptr || numSamples == 0) return {};

  if (sourceRate_ == targetRate_) {
    return std::vector<float>(input, input + numSamples);
  }

  const double ratio = static_cast<double>(targetRate_) / static_cast<double>(sourceRate_);
  const auto outSamples = static_cast<std::size_t>(std::ceil(numSamples * ratio));
  std::vector<float> output(outSamples);

  // Anti-aliasing cutoff factor (when downsampling, cutoff is scaled by ratio)
  const float cutoff = static_cast<float>(std::min(1.0, ratio) * 0.95);
  const auto kernelWidth = static_cast<int>(std::ceil(filterRadius_ / std::min(1.0, ratio)));

  for (std::size_t n = 0; n < outSamples; ++n) {
    const double inPos = n / ratio;
    const auto centerIdx = static_cast<int>(std::floor(inPos));

    float sampleSum = 0.0f;
    float weightSum = 0.0f;

    const int startIdx = std::max<int>(0, centerIdx - kernelWidth);
    const int endIdx = std::min<int>(static_cast<int>(numSamples) - 1, centerIdx + kernelWidth);

    for (int k = startIdx; k <= endIdx; ++k) {
      const float diff = static_cast<float>(inPos - k);
      const float distRatio = diff / static_cast<float>(kernelWidth);

      if (std::abs(distRatio) < 1.0f) {
        // Blackman window
        const float w = 0.42f + 0.50f * std::cos(std::numbers::pi_v<float> * distRatio) +
                        0.08f * std::cos(2.0f * std::numbers::pi_v<float> * distRatio);
        const float tap = sinc(diff * cutoff) * cutoff * w;

        sampleSum += input[k] * tap;
        weightSum += tap;
      }
    }

    if (std::abs(weightSum) > 1e-6f) {
      output[n] = sampleSum / weightSum;
    } else {
      output[n] = 0.0f;
    }
  }

  return output;
}

std::vector<float> AudioResampler::resample(const float* input,
                                            std::size_t numSamples,
                                            int inRate,
                                            int outRate) {
  AudioResampler r(inRate, outRate);
  return r.process(input, numSamples);
}

}  // namespace zyron::analysis
