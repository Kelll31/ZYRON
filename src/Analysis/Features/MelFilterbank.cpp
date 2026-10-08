// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Features/MelFilterbank.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::analysis {

MelFilterbank::MelFilterbank(Config config)
    : config_(config),
      stft_(StftConfig{
          .nFft = config.nFft,
          .hopLength = config.hopLength,
          .windowType = WindowType::HannPeriodic,
          .center = true,
          .scaleBySqrtN = true  // Divide by sqrt(1024) per Beat This!
      }) {
  initFilterbank();
}

float MelFilterbank::hzToMel(float hz) noexcept {
  constexpr float f_sp = 200.0f / 3.0f;
  constexpr float min_log_hz = 1000.0f;
  constexpr float min_log_mel = 15.0f;
  static const float logstep = std::log(6.4f) / 27.0f;

  if (hz < min_log_hz) {
    return hz / f_sp;
  }
  return min_log_mel + std::log(hz / min_log_hz) / logstep;
}

float MelFilterbank::melToHz(float mel) noexcept {
  constexpr float f_sp = 200.0f / 3.0f;
  constexpr float min_log_hz = 1000.0f;
  constexpr float min_log_mel = 15.0f;
  static const float logstep = std::log(6.4f) / 27.0f;

  if (mel < min_log_mel) {
    return mel * f_sp;
  }
  return min_log_hz * std::exp(logstep * (mel - min_log_mel));
}

void MelFilterbank::initFilterbank() {
  const std::size_t numBins = config_.nFft / 2 + 1;
  filterbank_.assign(config_.nMels, std::vector<float>(numBins, 0.0f));

  const float minMel = hzToMel(config_.fMin);
  const float maxMel = hzToMel(config_.fMax);

  // Generate nMels + 2 points linearly spaced in mel scale
  std::vector<float> hzPoints(config_.nMels + 2);
  for (std::size_t i = 0; i < config_.nMels + 2; ++i) {
    const float mel = minMel + static_cast<float>(i) * (maxMel - minMel) / static_cast<float>(config_.nMels + 1);
    hzPoints[i] = melToHz(mel);
  }

  // Precompute bin center frequencies in Hz
  std::vector<float> binFreqs(numBins);
  const float hzPerBin = static_cast<float>(config_.sampleRate) / static_cast<float>(config_.nFft);
  for (std::size_t k = 0; k < numBins; ++k) {
    binFreqs[k] = static_cast<float>(k) * hzPerBin;
  }

  // Create triangular filters
  for (std::size_t m = 0; m < config_.nMels; ++m) {
    const float fLow = hzPoints[m];
    const float fCenter = hzPoints[m + 1];
    const float fHigh = hzPoints[m + 2];

    const float slaneyNorm = config_.normSlaney ? (2.0f / (fHigh - fLow)) : 1.0f;

    for (std::size_t k = 0; k < numBins; ++k) {
      const float freq = binFreqs[k];
      float weight = 0.0f;

      if (freq >= fLow && freq <= fCenter && fCenter > fLow) {
        weight = (freq - fLow) / (fCenter - fLow);
      } else if (freq > fCenter && freq <= fHigh && fHigh > fCenter) {
        weight = (fHigh - freq) / (fHigh - fCenter);
      }

      filterbank_[m][k] = weight * slaneyNorm;
    }
  }
}

void MelFilterbank::applyToFrame(const float* powerSpectrum, float* melOut) const {
  const std::size_t bins = numBins();

  for (std::size_t m = 0; m < config_.nMels; ++m) {
    float sum = 0.0f;
    const auto& weights = filterbank_[m];
    for (std::size_t k = 0; k < bins; ++k) {
      sum += powerSpectrum[k] * weights[k];
    }

    if (config_.log1p1000) {
      melOut[m] = std::log1p(1000.0f * sum);
    } else {
      melOut[m] = sum;
    }
  }
}

std::vector<std::vector<float>> MelFilterbank::computeMelSpectrogram(
    const float* audio, std::size_t numSamples) const {
  const auto powerFrames = stft_.processPower(audio, numSamples);
  if (powerFrames.empty()) return {};

  const std::size_t numFrames = powerFrames.size();
  std::vector<std::vector<float>> melSpectrogram(numFrames, std::vector<float>(config_.nMels));

  for (std::size_t f = 0; f < numFrames; ++f) {
    applyToFrame(powerFrames[f].data(), melSpectrogram[f].data());
  }

  return melSpectrogram;
}

}  // namespace zyron::analysis
