// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Features/Resampler.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
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

  // One output sample: the windowed-sinc sum around input position `inPos` (the reference implementation, also used at
  // the edges of the signal where the kernel is cut off).
  const auto slowSample = [&](double inPos) {
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
    return std::abs(weightSum) > 1e-6f ? sampleSum / weightSum : 0.0f;
  };

  // For a rational ratio the fractional input position repeats every `phases` output samples, so the filter taps are
  // computed once per phase instead of once per output sample (the trigonometry dominated the run time).
  const auto divisor = std::gcd(sourceRate_, targetRate_);
  const auto phases = static_cast<std::size_t>(targetRate_ / divisor);
  const auto inputStep = static_cast<std::size_t>(sourceRate_ / divisor);  // input samples per `phases` outputs
  constexpr std::size_t kMaxPhases = 4096;
  const std::size_t span = static_cast<std::size_t>(2 * kernelWidth + 1);

  if (phases > kMaxPhases) {
    for (std::size_t n = 0; n < outSamples; ++n) {
      output[n] = slowSample(static_cast<double>(n) / ratio);
    }
    return output;
  }

  struct Phase {
    int center{0};  // input index of the kernel centre for the first period
    float norm{0.0f};
    std::vector<float> taps;  // 2 * kernelWidth + 1 weights, zero outside the window
  };
  std::vector<Phase> table(phases);
  for (std::size_t ph = 0; ph < phases; ++ph) {
    const double inPos = static_cast<double>(ph) / ratio;
    Phase& phase = table[ph];
    phase.center = static_cast<int>(std::floor(inPos));
    phase.taps.assign(span, 0.0f);
    for (int j = -kernelWidth; j <= kernelWidth; ++j) {
      const float diff = static_cast<float>(inPos - (phase.center + j));
      const float distRatio = diff / static_cast<float>(kernelWidth);
      if (std::abs(distRatio) < 1.0f) {
        const float w = 0.42f + 0.50f * std::cos(std::numbers::pi_v<float> * distRatio) +
                        0.08f * std::cos(2.0f * std::numbers::pi_v<float> * distRatio);
        const float tap = sinc(diff * cutoff) * cutoff * w;
        phase.taps[static_cast<std::size_t>(j + kernelWidth)] = tap;
        phase.norm += tap;
      }
    }
  }

  const auto total = static_cast<long long>(numSamples);
  for (std::size_t n = 0; n < outSamples; ++n) {
    const std::size_t period = n / phases;
    const Phase& phase = table[n % phases];
    const long long center = static_cast<long long>(phase.center) + static_cast<long long>(period * inputStep);
    if (center - kernelWidth < 0 || center + kernelWidth >= total || std::abs(phase.norm) <= 1e-6f) {
      output[n] = slowSample(static_cast<double>(n) / ratio);  // the kernel is cut off at the edge of the signal
      continue;
    }
    const float* source = input + (center - kernelWidth);
    float sum = 0.0f;
    for (std::size_t i = 0; i < span; ++i) {
      sum += source[i] * phase.taps[i];
    }
    output[n] = sum / phase.norm;
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
