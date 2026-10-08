// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Features/Cqt.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace zyron::analysis {

ConstantQTransform::ConstantQTransform(Config config)
    : config_(config) {
  const float ratio = std::pow(2.0f, 1.0f / static_cast<float>(config_.binsPerOctave));
  q_ = 1.0f / (ratio - 1.0f);
  initKernels();
}

float ConstantQTransform::centerFrequency(std::size_t bin) const noexcept {
  return config_.fMin * std::pow(2.0f, static_cast<float>(bin) / static_cast<float>(config_.binsPerOctave));
}

void ConstantQTransform::initKernels() {
  kernels_.resize(config_.numBins);

  for (std::size_t k = 0; k < config_.numBins; ++k) {
    const float fk = centerFrequency(k);
    const auto nk = static_cast<std::size_t>(
        std::max(8.0f, std::round(q_ * static_cast<float>(config_.sampleRate) / fk)));

    Kernel kernel;
    kernel.temporal.resize(nk);
    kernel.normFactor = 1.0f / static_cast<float>(nk);

    for (std::size_t n = 0; n < nk; ++n) {
      // Hann window
      const float w = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(n) / static_cast<float>(nk)));
      const float angle = 2.0f * std::numbers::pi_v<float> * q_ * static_cast<float>(n) / static_cast<float>(nk);
      kernel.temporal[n] = std::complex<float>(std::cos(-angle) * w, std::sin(-angle) * w);
    }

    kernels_[k] = std::move(kernel);
  }
}

std::vector<std::vector<float>> ConstantQTransform::processMagnitude(
    const float* audio, std::size_t numSamples) const {
  if (audio == nullptr || numSamples == 0) return {};

  const std::size_t numFrames = (numSamples + config_.hopLength - 1) / config_.hopLength;
  std::vector<std::vector<float>> result(numFrames, std::vector<float>(config_.numBins, 0.0f));

  for (std::size_t f = 0; f < numFrames; ++f) {
    const auto centerPos = static_cast<std::int64_t>(f * config_.hopLength);

    for (std::size_t k = 0; k < config_.numBins; ++k) {
      const auto& kernel = kernels_[k];
      const auto nk = static_cast<std::int64_t>(kernel.temporal.size());
      const std::int64_t start = centerPos - nk / 2;

      std::complex<float> sum(0.0f, 0.0f);
      for (std::int64_t n = 0; n < nk; ++n) {
        const std::int64_t audioIdx = start + n;
        if (audioIdx >= 0 && audioIdx < static_cast<std::int64_t>(numSamples)) {
          sum += audio[audioIdx] * kernel.temporal[static_cast<std::size_t>(n)];
        }
      }

      result[f][k] = std::abs(sum) * kernel.normFactor;
    }
  }

  return result;
}

}  // namespace zyron::analysis
