// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Energy/EnergyAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <vector>

#include "Analysis/Features/Fft.hpp"

namespace zyron::analysis {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kFftSize = 1024;

struct WindowMetrics {
  float loudnessScore{0.0f};
  float spectralScore{0.0f};
  float transientScore{0.0f};
  float bassScore{0.0f};
  float energy{1.0f};
};

WindowMetrics computeMetrics(const float* audio, std::size_t numSamples, int sampleRate, const Fft& fft) {
  WindowMetrics m;
  if (!audio || numSamples < 64 || sampleRate <= 0) {
    return m;
  }

  // 1. RMS Loudness
  double sumSq = 0.0;
  for (std::size_t i = 0; i < numSamples; ++i) {
    const double val = audio[i];
    sumSq += val * val;
  }
  const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
  if (rms < 1e-5f) {
    // Virtually silent
    m.energy = 1.0f;
    return m;
  }

  const float dB = 20.0f * std::log10(rms + 1e-9f);
  // Map -40.0 dBFS (very quiet) .. -6.0 dBFS (loud mastered EDM) to 0.0 .. 1.0
  m.loudnessScore = std::clamp((dB - (-40.0f)) / (-6.0f - (-40.0f)), 0.0f, 1.0f);

  // 2. Transient Density (rectified derivative RMS / audio RMS)
  double sumTransSq = 0.0;
  for (std::size_t i = 1; i < numSamples; ++i) {
    const float diff = std::abs(audio[i]) - std::abs(audio[i - 1]);
    const float positiveDiff = (diff > 0.0f) ? diff : 0.0f;
    sumTransSq += static_cast<double>(positiveDiff * positiveDiff);
  }
  const float transRms = static_cast<float>(std::sqrt(sumTransSq / static_cast<double>(numSamples - 1)));
  const float transRatio = transRms / (rms + 1e-9f);
  // Typical transient ratio in percussive music: 0.05 (sustained drone) .. 0.35 (dense snappy percussion)
  m.transientScore = std::clamp((transRatio - 0.05f) / (0.35f - 0.05f), 0.0f, 1.0f);

  // 3. Spectral Analysis via FFT (Centroid & Bass ratio)
  std::vector<float> window(kFftSize, 0.0f);
  const std::size_t copyLen = std::min(numSamples, kFftSize);
  for (std::size_t i = 0; i < copyLen; ++i) {
    const float hann = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFftSize - 1)));
    window[i] = audio[i] * hann;
  }

  std::vector<std::complex<float>> spectrum(kFftSize / 2 + 1);
  fft.forwardReal(window.data(), spectrum.data());

  const float binFreq = static_cast<float>(sampleRate) / static_cast<float>(kFftSize);
  double totalMag = 0.0;
  double weightedFreqSum = 0.0;
  double bassEnergy = 0.0;
  double totalEnergy = 0.0;

  for (std::size_t bin = 1; bin < spectrum.size(); ++bin) {
    const float mag = std::abs(spectrum[bin]);
    const float energy = mag * mag;
    const float freq = static_cast<float>(bin) * binFreq;

    totalMag += mag;
    weightedFreqSum += static_cast<double>(freq * mag);
    totalEnergy += energy;

    if (freq >= 20.0f && freq <= 250.0f) {
      bassEnergy += energy;
    }
  }

  // Spectral Centroid
  float centroid = 500.0f;
  if (totalMag > 1e-8) {
    centroid = static_cast<float>(weightedFreqSum / totalMag);
  }
  // Map 500 Hz (sub/dark) .. 4500 Hz (bright, intense treble & cymbals) to 0.0 .. 1.0
  m.spectralScore = std::clamp((centroid - 500.0f) / (4500.0f - 500.0f), 0.0f, 1.0f);

  // Bass Ratio
  float bassRatio = 0.0f;
  if (totalEnergy > 1e-8) {
    bassRatio = static_cast<float>(bassEnergy / totalEnergy);
  }
  // Map 0.10 (thin breakdown) .. 0.65 (heavy sub-bass drop) to 0.0 .. 1.0
  m.bassScore = std::clamp((bassRatio - 0.10f) / (0.65f - 0.10f), 0.0f, 1.0f);

  // Weighted composite score
  const float composite = 0.40f * m.loudnessScore +
                          0.25f * m.bassScore +
                          0.20f * m.transientScore +
                          0.15f * m.spectralScore;

  m.energy = std::clamp(1.0f + 9.0f * composite, 1.0f, 10.0f);
  return m;
}

}  // namespace

float EnergyAnalyzer::computeWindowEnergy(const float* audio,
                                          std::size_t numSamples,
                                          int sampleRate) const {
  Fft fft(kFftSize);
  const auto metrics = computeMetrics(audio, numSamples, sampleRate, fft);
  return metrics.energy;
}

EnergyResult EnergyAnalyzer::analyze(const float* audio,
                                     std::size_t numSamples,
                                     int sampleRate,
                                     float stepSec) const {
  EnergyResult result;
  result.curveStepSec = (stepSec > 0.05f) ? stepSec : 0.5f;

  if (!audio || numSamples == 0 || sampleRate <= 0) {
    result.globalEnergy = 1.0f;
    return result;
  }

  Fft fft(kFftSize);
  const std::size_t stepSamples = static_cast<std::size_t>(result.curveStepSec * static_cast<float>(sampleRate));
  if (stepSamples == 0) {
    result.globalEnergy = 1.0f;
    return result;
  }

  std::vector<WindowMetrics> allMetrics;
  std::vector<float> rawCurve;

  for (std::size_t offset = 0; offset < numSamples; offset += stepSamples) {
    const std::size_t remaining = numSamples - offset;
    const std::size_t windowLen = std::min(stepSamples, remaining);
    if (windowLen < 64) break;

    const auto m = computeMetrics(audio + offset, windowLen, sampleRate, fft);
    allMetrics.push_back(m);
    rawCurve.push_back(m.energy);
  }

  if (rawCurve.empty()) {
    result.globalEnergy = 1.0f;
    return result;
  }

  // Smooth curve with 3-point moving average to avoid single-frame transient spikes
  result.energyCurve.resize(rawCurve.size());
  for (std::size_t i = 0; i < rawCurve.size(); ++i) {
    const float prev = (i > 0) ? rawCurve[i - 1] : rawCurve[i];
    const float curr = rawCurve[i];
    const float next = (i + 1 < rawCurve.size()) ? rawCurve[i + 1] : rawCurve[i];
    result.energyCurve[i] = std::clamp(0.25f * prev + 0.50f * curr + 0.25f * next, 1.0f, 10.0f);
  }

  // Calculate 85th percentile for global track dancefloor energy (SPEC section 53, 56)
  std::vector<float> sortedCurve = result.energyCurve;
  std::sort(sortedCurve.begin(), sortedCurve.end());
  const std::size_t p85Idx = std::min(sortedCurve.size() - 1,
                                      static_cast<std::size_t>(0.85f * static_cast<float>(sortedCurve.size())));
  result.globalEnergy = std::clamp(sortedCurve[p85Idx], 1.0f, 10.0f);

  // Average factor breakdowns over active high-energy sections (upper 50% percentile)
  const float medianVal = sortedCurve[sortedCurve.size() / 2];
  float sumL = 0.0f, sumS = 0.0f, sumT = 0.0f, sumB = 0.0f;
  std::size_t activeCount = 0;

  for (const auto& m : allMetrics) {
    if (m.energy >= medianVal) {
      sumL += m.loudnessScore;
      sumS += m.spectralScore;
      sumT += m.transientScore;
      sumB += m.bassScore;
      ++activeCount;
    }
  }

  if (activeCount > 0) {
    const float inv = 1.0f / static_cast<float>(activeCount);
    result.loudnessScore = sumL * inv;
    result.spectralScore = sumS * inv;
    result.transientScore = sumT * inv;
    result.bassScore = sumB * inv;
  }

  return result;
}

}  // namespace zyron::analysis
