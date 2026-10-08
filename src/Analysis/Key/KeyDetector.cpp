// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Key/KeyDetector.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "Analysis/Features/Cqt.hpp"
#include "Analysis/Features/Resampler.hpp"

namespace zyron::analysis {

namespace {

// Krumhansl-Schmuckler key profiles
constexpr float kMajorProfile[12] = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};

constexpr float kMinorProfile[12] = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

float pearsonCorrelation(const float* x, const float* y, std::size_t n) {
  if (n == 0) return 0.0f;
  float sumX = 0.0f, sumY = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    sumX += x[i];
    sumY += y[i];
  }
  const float meanX = sumX / static_cast<float>(n);
  const float meanY = sumY / static_cast<float>(n);

  float num = 0.0f, denX = 0.0f, denY = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    const float dx = x[i] - meanX;
    const float dy = y[i] - meanY;
    num += dx * dy;
    denX += dx * dx;
    denY += dy * dy;
  }
  const float den = std::sqrt(denX * denY);
  return (den > 1e-8f) ? (num / den) : 0.0f;
}

}  // namespace

std::vector<float> KeyDetector::computeChromaProfile(const float* audio,
                                                     std::size_t numSamples,
                                                     int sampleRate) const {
  if (!audio || numSamples < 2048 || sampleRate <= 0) {
    return {};
  }

  // 1. Resample to 22.05 kHz mono if needed (per S-KEY contract)
  std::vector<float> resampled;
  const float* audio22k = audio;
  std::size_t samples22k = numSamples;

  if (sampleRate != 22050) {
    resampled = AudioResampler::resample(audio, numSamples, sampleRate, 22050);
    audio22k = resampled.data();
    samples22k = resampled.size();
  }

  if (samples22k < 2048) {
    return {};
  }

  // 2. Peak normalization (S-KEY contract)
  float maxAbs = 0.0f;
  for (std::size_t i = 0; i < samples22k; ++i) {
    const float a = std::abs(audio22k[i]);
    if (a > maxAbs) maxAbs = a;
  }

  if (maxAbs < 1e-6f) {
    return {};
  }

  std::vector<float> normalizedAudio(samples22k);
  const float normScale = 1.0f / maxAbs;
  for (std::size_t i = 0; i < samples22k; ++i) {
    normalizedAudio[i] = audio22k[i] * normScale;
  }

  // 3. Constant-Q Transform (144 bins = 24 bins/octave, 6 octaves from 32.7 Hz ~ C1)
  ConstantQTransform cqt;
  const auto cqtFrames = cqt.processMagnitude(normalizedAudio.data(), normalizedAudio.size());
  if (cqtFrames.empty()) {
    return {};
  }

  // 4. Fold 144 frequency bins into 12 pitch classes (C, C#, D, D#, E, F, F#, G, G#, A, A#, B)
  // Since CQT starts at 32.703 Hz (C1) with 24 bins per octave (2 bins per semitone, bin 0 = C1),
  // semitone center is at (bin + 1) / 2:
  std::vector<float> chroma(12, 0.0f);
  for (const auto& frame : cqtFrames) {
    for (std::size_t bin = 0; bin < frame.size(); ++bin) {
      const std::size_t semitone = ((bin + 1) / 2) % 12;
      chroma[semitone] += frame[bin];
    }
  }

  // Normalize chroma vector to sum to 1.0
  const float sum = std::accumulate(chroma.begin(), chroma.end(), 0.0f);
  if (sum > 1e-6f) {
    for (float& c : chroma) {
      c /= sum;
    }
  } else {
    return {};
  }

  return chroma;
}

MusicalKey KeyDetector::detectKey(const float* audio,
                                  std::size_t numSamples,
                                  int sampleRate) const {
  if (!audio || numSamples < 2048 || sampleRate <= 0) {
    return {};
  }
  const auto chroma = computeChromaProfile(audio, numSamples, sampleRate);
  if (chroma.empty()) return {};

  float bestCorr = -2.0f;
  int bestClassIndex = 0;

  // S-KEY class index mapping:
  // 0..11: Major keys (C=0, Db=1, D=2, Eb=3, E=4, F=5, F#=6, G=7, Ab=8, A=9, Bb=10, B=11)
  // 12..23: Minor keys (Cm=12, C#m=13, Dm=14, Ebm=15, Em=16, Fm=17, F#m=18, Gm=19, Abm=20, Am=21, Bbm=22, Bm=23)

  for (int root = 0; root < 12; ++root) {
    // 1. Major test
    float shiftedMajor[12];
    for (int i = 0; i < 12; ++i) {
      shiftedMajor[(root + i) % 12] = kMajorProfile[i];
    }
    const float corrMajor = pearsonCorrelation(chroma.data(), shiftedMajor, 12);
    if (corrMajor > bestCorr) {
      bestCorr = corrMajor;
      bestClassIndex = root;  // Major class
    }

    // 2. Minor test
    float shiftedMinor[12];
    for (int i = 0; i < 12; ++i) {
      shiftedMinor[(root + i) % 12] = kMinorProfile[i];
    }
    const float corrMinor = pearsonCorrelation(chroma.data(), shiftedMinor, 12);
    if (corrMinor > bestCorr) {
      bestCorr = corrMinor;
      bestClassIndex = 12 + root;  // Minor class
    }
  }

  const float confidence = std::clamp((bestCorr + 1.0f) * 0.5f, 0.0f, 1.0f);
  return MusicalKey::fromClassIndex(bestClassIndex, confidence);
}

}  // namespace zyron::analysis
