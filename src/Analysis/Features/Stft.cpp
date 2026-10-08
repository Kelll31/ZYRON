// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Features/Stft.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace zyron::analysis {

Stft::Stft(StftConfig config)
    : config_(config), fft_(config.nFft) {
  initWindow();
  normScale_ = config_.scaleBySqrtN
                   ? (1.0f / std::sqrt(static_cast<float>(config_.nFft)))
                   : 1.0f;
}

void Stft::initWindow() {
  window_.resize(config_.nFft);
  const auto N = static_cast<float>(config_.nFft);

  for (std::size_t i = 0; i < config_.nFft; ++i) {
    const auto n = static_cast<float>(i);
    switch (config_.windowType) {
      case WindowType::HannPeriodic:
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * n / N));
        break;
      case WindowType::HannSymmetric:
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * n / (N - 1.0f)));
        break;
      case WindowType::Hamming:
        window_[i] = 0.54f - 0.46f * std::cos(2.0f * std::numbers::pi_v<float> * n / (N - 1.0f));
        break;
      case WindowType::Rectangular:
        window_[i] = 1.0f;
        break;
    }
  }
}

std::vector<float> Stft::padReflect(const float* audio, std::size_t numSamples) const {
  if (numSamples == 0) return {};
  if (!config_.center) {
    return std::vector<float>(audio, audio + numSamples);
  }

  const std::size_t pad = config_.nFft / 2;
  std::vector<float> padded(numSamples + 2 * pad);

  // Left reflect padding (e.g. [3, 2, 1] for signal [0, 1, 2, 3...])
  for (std::size_t i = 0; i < pad; ++i) {
    std::size_t srcIdx = 0;
    if (numSamples > 1) {
      srcIdx = (pad - i) % (2 * (numSamples - 1));
      if (srcIdx >= numSamples) {
        srcIdx = 2 * (numSamples - 1) - srcIdx;
      }
    }
    padded[i] = audio[srcIdx];
  }

  // Center
  std::copy(audio, audio + numSamples, padded.begin() + pad);

  // Right reflect padding
  for (std::size_t i = 0; i < pad; ++i) {
    std::size_t srcIdx = numSamples - 1;
    if (numSamples > 1) {
      const std::size_t offset = i + 1;
      const std::size_t rem = offset % (2 * (numSamples - 1));
      if (rem < numSamples) {
        srcIdx = (numSamples - 1) - rem;
      } else {
        srcIdx = rem - (numSamples - 1);
      }
    }
    padded[pad + numSamples + i] = audio[srcIdx];
  }

  return padded;
}

std::vector<std::vector<std::complex<float>>> Stft::processComplex(const float* audio,
                                                                   std::size_t numSamples) const {
  if (audio == nullptr || numSamples == 0) return {};

  const auto signal = padReflect(audio, numSamples);
  if (signal.size() < config_.nFft) return {};

  const std::size_t numFrames = 1 + (signal.size() - config_.nFft) / config_.hopLength;
  const std::size_t bins = numBins();

  std::vector<std::vector<std::complex<float>>> frames(numFrames, std::vector<std::complex<float>>(bins));
  std::vector<float> windowed(config_.nFft);

  for (std::size_t f = 0; f < numFrames; ++f) {
    const std::size_t offset = f * config_.hopLength;
    for (std::size_t i = 0; i < config_.nFft; ++i) {
      windowed[i] = signal[offset + i] * window_[i] * normScale_;
    }

    fft_.forwardReal(windowed.data(), frames[f].data());
  }

  return frames;
}

std::vector<std::vector<float>> Stft::processMagnitude(const float* audio,
                                                       std::size_t numSamples) const {
  const auto complexFrames = processComplex(audio, numSamples);
  if (complexFrames.empty()) return {};

  const std::size_t numFrames = complexFrames.size();
  const std::size_t bins = numBins();

  std::vector<std::vector<float>> magnitude(numFrames, std::vector<float>(bins));
  for (std::size_t f = 0; f < numFrames; ++f) {
    for (std::size_t b = 0; b < bins; ++b) {
      magnitude[f][b] = std::abs(complexFrames[f][b]);
    }
  }

  return magnitude;
}

std::vector<std::vector<float>> Stft::processPower(const float* audio,
                                                   std::size_t numSamples) const {
  const auto complexFrames = processComplex(audio, numSamples);
  if (complexFrames.empty()) return {};

  const std::size_t numFrames = complexFrames.size();
  const std::size_t bins = numBins();

  std::vector<std::vector<float>> power(numFrames, std::vector<float>(bins));
  for (std::size_t f = 0; f < numFrames; ++f) {
    for (std::size_t b = 0; b < bins; ++b) {
      power[f][b] = std::norm(complexFrames[f][b]);
    }
  }

  return power;
}

std::vector<float> Stft::processInverse(
    const std::vector<std::vector<std::complex<float>>>& complexFrames,
    std::size_t expectedSamples) const {
  if (complexFrames.empty()) return {};

  const std::size_t numFrames = complexFrames.size();
  const std::size_t totalPaddedLength = (numFrames - 1) * config_.hopLength + config_.nFft;

  std::vector<float> reconstructed(totalPaddedLength, 0.0f);
  std::vector<float> windowSum(totalPaddedLength, 0.0f);
  std::vector<float> frameTime(config_.nFft);

  const float invNormScale = (normScale_ > 0.0f) ? (1.0f / normScale_) : 1.0f;

  for (std::size_t f = 0; f < numFrames; ++f) {
    const std::size_t offset = f * config_.hopLength;
    fft_.inverseReal(complexFrames[f].data(), frameTime.data());

    for (std::size_t i = 0; i < config_.nFft; ++i) {
      const float w = window_[i];
      reconstructed[offset + i] += frameTime[i] * w * invNormScale;
      windowSum[offset + i] += w * w;
    }
  }

  // Normalize by window overlap
  for (std::size_t i = 0; i < totalPaddedLength; ++i) {
    if (windowSum[i] > 1e-5f) {
      reconstructed[i] /= windowSum[i];
    }
  }

  // Trim center padding
  if (config_.center) {
    const std::size_t pad = config_.nFft / 2;
    if (reconstructed.size() > 2 * pad) {
      std::size_t outLen = reconstructed.size() - 2 * pad;
      if (expectedSamples > 0 && expectedSamples < outLen) {
        outLen = expectedSamples;
      }
      return std::vector<float>(reconstructed.begin() + pad, reconstructed.begin() + pad + outLen);
    }
  }

  if (expectedSamples > 0 && expectedSamples < reconstructed.size()) {
    reconstructed.resize(expectedSamples);
  }
  return reconstructed;
}

}  // namespace zyron::analysis
