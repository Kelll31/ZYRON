// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Bpm/BeatDetector.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <vector>

#include "Analysis/Features/MelFilterbank.hpp"
#include "Analysis/Features/Resampler.hpp"

namespace zyron::analysis {

namespace {

float interpolateAutocorr(const std::vector<float>& autocorr, double lag) {
  if (lag <= 0.0) return 0.0f;
  const auto idx = static_cast<std::size_t>(lag);
  if (idx + 1 >= autocorr.size()) {
    return (idx < autocorr.size()) ? autocorr[idx] : 0.0f;
  }
  const float t = static_cast<float>(lag - static_cast<double>(idx));

  const float p0 = (idx > 0) ? autocorr[idx - 1] : autocorr[idx];
  const float p1 = autocorr[idx];
  const float p2 = autocorr[idx + 1];
  const float p3 = (idx + 2 < autocorr.size()) ? autocorr[idx + 2] : p2;

  // Catmull-Rom cubic spline
  const float a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
  const float a1 =  1.0f * p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
  const float a2 = -0.5f * p0 + 0.5f * p2;
  const float a3 = p1;

  return ((a0 * t + a1) * t + a2) * t + a3;
}

std::string formatGridJson(double bpm, std::int64_t firstBeatFrame, int downbeatOffset,
                           int sampleRate, std::size_t beatCount) {
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(2);
  ss << "{\"bpm\":" << bpm
     << ",\"firstBeatFrame\":" << firstBeatFrame
     << ",\"downbeatOffset\":" << downbeatOffset
     << ",\"sampleRate\":" << sampleRate
     << ",\"beatCount\":" << beatCount
     << "}";
  return ss.str();
}

}  // namespace

double BeatDetector::resolveTempo(double rawBpm, TempoPriorMode prior) noexcept {
  if (rawBpm <= 0.0) return 0.0;

  switch (prior) {
    case TempoPriorMode::DnB: {
      // DnB target range: 160.0 .. 180.0 BPM (§15, ADR-0010)
      // Standard tempo is ~174 BPM (half-time: 87, double-time: 348, quarter-time: 43.5)
      constexpr double kTargetCenter = 174.0;
      double bestBpm = rawBpm;
      double minDiff = 1e9;

      const double multipliers[] = {0.25, 0.5, 1.0, 2.0, 4.0};
      for (double m : multipliers) {
        const double candidate = rawBpm * m;
        if (candidate >= 150.0 && candidate <= 190.0) {
          const double diff = std::abs(candidate - kTargetCenter);
          if (diff < minDiff) {
            minDiff = diff;
            bestBpm = candidate;
          }
        }
      }
      if (minDiff < 1e8) {
        return bestBpm;
      }
      // If none fell into [150, 190], bring it into [110, 220] range via octave jumps
      while (bestBpm < 110.0) bestBpm *= 2.0;
      while (bestBpm > 220.0) bestBpm /= 2.0;
      return bestBpm;
    }

    case TempoPriorMode::HouseTechno: {
      // House / Techno target: 120.0 .. 132.0 BPM, center ~126.0
      constexpr double kTargetCenter = 126.0;
      double bestBpm = rawBpm;
      double minDiff = 1e9;
      const double multipliers[] = {0.25, 0.5, 1.0, 2.0, 4.0};
      for (double m : multipliers) {
        const double candidate = rawBpm * m;
        if (candidate >= 115.0 && candidate <= 138.0) {
          const double diff = std::abs(candidate - kTargetCenter);
          if (diff < minDiff) {
            minDiff = diff;
            bestBpm = candidate;
          }
        }
      }
      if (minDiff < 1e8) return bestBpm;
      while (bestBpm < 90.0) bestBpm *= 2.0;
      while (bestBpm > 180.0) bestBpm /= 2.0;
      return bestBpm;
    }

    case TempoPriorMode::HipHop: {
      // Hip Hop target: 80.0 .. 115.0 BPM, center ~90.0
      constexpr double kTargetCenter = 90.0;
      double bestBpm = rawBpm;
      double minDiff = 1e9;
      const double multipliers[] = {0.25, 0.5, 1.0, 2.0, 4.0};
      for (double m : multipliers) {
        const double candidate = rawBpm * m;
        if (candidate >= 70.0 && candidate <= 120.0) {
          const double diff = std::abs(candidate - kTargetCenter);
          if (diff < minDiff) {
            minDiff = diff;
            bestBpm = candidate;
          }
        }
      }
      if (minDiff < 1e8) return bestBpm;
      while (bestBpm < 60.0) bestBpm *= 2.0;
      while (bestBpm > 140.0) bestBpm /= 2.0;
      return bestBpm;
    }

    case TempoPriorMode::None:
    default: {
      double bpm = rawBpm;
      while (bpm < 60.0) bpm *= 2.0;
      while (bpm > 200.0) bpm /= 2.0;
      return bpm;
    }
  }
}

std::vector<std::size_t> BeatDetector::pickPeaks(const float* logits,
                                                 std::size_t numFrames,
                                                 float threshold,
                                                 int radius) {
  std::vector<std::size_t> peaks;
  if (!logits || numFrames == 0 || radius <= 0) return peaks;

  for (std::size_t i = 0; i < numFrames; ++i) {
    const float val = logits[i];
    if (val <= threshold) continue;

    bool isPeak = true;
    const std::size_t start = (i >= static_cast<std::size_t>(radius)) ? (i - radius) : 0;
    const std::size_t end = std::min(numFrames - 1, i + radius);

    for (std::size_t j = start; j <= end; ++j) {
      if (j == i) continue;
      if (j < i && logits[j] >= val) {
        isPeak = false;
        break;
      }
      if (j > i && logits[j] > val) {
        isPeak = false;
        break;
      }
    }

    if (isPeak) {
      peaks.push_back(i);
    }
  }
  return peaks;
}

BeatDetectionResult BeatDetector::buildGridFromBeats(
    const std::vector<std::size_t>& beatFrames50Fps,
    const std::vector<std::size_t>& downbeatFrames50Fps,
    int originalSampleRate,
    std::size_t originalNumSamples,
    TempoPriorMode prior) {
  BeatDetectionResult result;
  if (beatFrames50Fps.size() < 2 || originalSampleRate <= 0) {
    return result;
  }

  // Linear regression of beat frame positions against beat indices yields
  // high-accuracy period estimation without integer rounding distortion:
  const double n = static_cast<double>(beatFrames50Fps.size());
  double sumX = 0.0;
  double sumY = 0.0;
  double sumXY = 0.0;
  double sumX2 = 0.0;
  for (std::size_t i = 0; i < beatFrames50Fps.size(); ++i) {
    const double x = static_cast<double>(i);
    const double y = static_cast<double>(beatFrames50Fps[i]);
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
  }

  const double denom = n * sumX2 - sumX * sumX;
  const double slope = (denom != 0.0) ? ((n * sumXY - sumX * sumY) / denom) : 0.0;
  if (slope <= 0.0) return result;

  // 50 FPS frame rate
  const double rawBpm = 3000.0 / slope;
  const double resolvedBpm = resolveTempo(rawBpm, prior);
  if (resolvedBpm <= 0.0) return result;

  const double samplesPerBeat = (static_cast<double>(originalSampleRate) * 60.0) / resolvedBpm;
  const double firstBeat50Fps = static_cast<double>(beatFrames50Fps.front());
  const auto firstBeatFrame = static_cast<std::int64_t>(
      std::round(firstBeat50Fps * (static_cast<double>(originalSampleRate) / 50.0)));

  // Generate beat grid
  std::vector<std::int64_t> beatFrames;
  std::vector<std::int64_t> downbeatFrames;

  int downbeatOffset = 0;
  if (!downbeatFrames50Fps.empty()) {
    const double firstDownbeat50Fps = static_cast<double>(downbeatFrames50Fps.front());
    const double beatIndexDouble = (firstDownbeat50Fps - firstBeat50Fps) / (3000.0 / resolvedBpm);
    const int nearestBeatIndex = static_cast<int>(std::round(beatIndexDouble));
    downbeatOffset = (nearestBeatIndex % 4 + 4) % 4;
  }

  std::size_t beatIdx = 0;
  while (true) {
    const auto frame = firstBeatFrame + static_cast<std::int64_t>(std::round(beatIdx * samplesPerBeat));
    if (frame >= static_cast<std::int64_t>(originalNumSamples)) {
      break;
    }
    beatFrames.push_back(frame);
    if ((static_cast<int>(beatIdx) % 4) == downbeatOffset) {
      downbeatFrames.push_back(frame);
    }
    beatIdx++;
  }

  result.bpm = resolvedBpm;
  result.firstBeatFrame = firstBeatFrame;
  result.beatFrames = std::move(beatFrames);
  result.downbeatFrames = std::move(downbeatFrames);
  result.gridJson = formatGridJson(result.bpm, result.firstBeatFrame, downbeatOffset,
                                   originalSampleRate, result.beatFrames.size());
  result.success = !result.beatFrames.empty();
  return result;
}

BeatDetectionResult BeatDetector::detect(const float* audio,
                                         std::size_t numSamples,
                                         int sampleRate,
                                         TempoPriorMode prior) const {
  BeatDetectionResult result;
  if (!audio || numSamples < 4410 || sampleRate <= 0) {
    return result;
  }

  // 1. Resample to 22.05 kHz mono for Beat This! mel front-end
  std::vector<float> resampled;
  const float* audio22k = audio;
  std::size_t samples22k = numSamples;

  if (sampleRate != 22050) {
    resampled = AudioResampler::resample(audio, numSamples, sampleRate, 22050);
    audio22k = resampled.data();
    samples22k = resampled.size();
  }

  if (samples22k < 2205) {  // less than 0.1s
    return result;
  }

  // 2. Mel spectrogram via MelFilterbank (1024 FFT, 441 hop = 50 FPS, 128 Slaney mels, log1p1000)
  MelFilterbank melFilter;
  const auto spec = melFilter.computeMelSpectrogram(audio22k, samples22k);
  const std::size_t numFrames = spec.size();
  if (numFrames < 10) {
    return result;
  }

  // 3. Spectral novelty / flux curve (positive half-wave difference across mel bins)
  std::vector<float> flux(numFrames, 0.0f);
  for (std::size_t t = 1; t < numFrames; ++t) {
    float diffSum = 0.0f;
    for (std::size_t m = 0; m < 128; ++m) {
      const float diff = spec[t][m] - spec[t - 1][m];
      if (diff > 0.0f) {
        diffSum += diff;
      }
    }
    flux[t] = diffSum;
  }

  // 4. Adaptive local mean subtraction (moving window of 15 frames ~ 300 ms)
  std::vector<float> novelty(numFrames, 0.0f);
  constexpr int kHalfWindow = 7;
  float maxNovelty = 0.0f;

  for (std::size_t t = 0; t < numFrames; ++t) {
    const std::size_t winStart = (t >= static_cast<std::size_t>(kHalfWindow)) ? (t - kHalfWindow) : 0;
    const std::size_t winEnd = std::min(numFrames - 1, t + kHalfWindow);
    float sum = 0.0f;
    for (std::size_t j = winStart; j <= winEnd; ++j) {
      sum += flux[j];
    }
    const float localMean = sum / static_cast<float>(winEnd - winStart + 1);
    const float val = std::max(0.0f, flux[t] - localMean);
    novelty[t] = val;
    if (val > maxNovelty) {
      maxNovelty = val;
    }
  }

  if (maxNovelty < 1e-4f) {
    return result;  // Silence or flat signal
  }

  // 5. Normalized autocorrelation of novelty curve
  // Period for 50 .. 220 BPM at 50 FPS corresponds to 13.6 .. 60 frames.
  // We evaluate lags up to 250 frames for 4 harmonics.
  const std::size_t maxLag = std::min(numFrames / 2, std::size_t{250});
  std::vector<float> autocorr(maxLag + 1, 0.0f);

  for (std::size_t lag = 1; lag <= maxLag; ++lag) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t t = 0; t + lag < numFrames; ++t) {
      sum += static_cast<double>(novelty[t]) * static_cast<double>(novelty[t + lag]);
      count++;
    }
    if (count > 0) {
      autocorr[lag] = static_cast<float>(sum / count);
    }
  }

  // 6. Comb filter harmonic evaluation over candidate BPM range [60.0 .. 200.0]
  auto calcScore = [&](double period) -> double {
    double s = 1.0 * interpolateAutocorr(autocorr, period);
    if (period * 2.0 <= static_cast<double>(maxLag)) {
      s += 0.75 * interpolateAutocorr(autocorr, period * 2.0);
    }
    if (period * 3.0 <= static_cast<double>(maxLag)) {
      s += 0.50 * interpolateAutocorr(autocorr, period * 3.0);
    }
    if (period * 4.0 <= static_cast<double>(maxLag)) {
      s += 0.35 * interpolateAutocorr(autocorr, period * 4.0);
    }
    return s;
  };

  constexpr double kBpmMin = 60.0;
  constexpr double kBpmMax = 200.0;
  constexpr double kBpmStep = 0.05;

  double bestBpmCandidate = 120.0;
  double bestScore = -1.0;

  for (double bpm = kBpmMin; bpm <= kBpmMax; bpm += kBpmStep) {
    const double period = 3000.0 / bpm;
    const double score = calcScore(period);
    if (score > bestScore) {
      bestScore = score;
      bestBpmCandidate = bpm;
    }
  }

  // Refine peak via 3-point parabolic interpolation on comb score
  const double periodPrev = 3000.0 / (bestBpmCandidate - kBpmStep);
  const double periodNext = 3000.0 / (bestBpmCandidate + kBpmStep);
  const double scorePrev = calcScore(periodPrev);
  const double scoreNext = calcScore(periodNext);

  double refinedBpm = bestBpmCandidate;
  const double denom = scorePrev - 2.0 * bestScore + scoreNext;
  if (denom < -1e-8) {
    const double delta = (0.5 * (scorePrev - scoreNext)) / denom;
    if (std::abs(delta) < 1.0) {
      refinedBpm += delta * kBpmStep;
    }
  }

  // 7. Resolve octave ambiguities via genre tempo prior
  const double finalBpm = resolveTempo(refinedBpm, prior);
  if (finalBpm <= 0.0) {
    return result;
  }

  const double beatPeriodFrames = 3000.0 / finalBpm;

  // 8. Find optimal phase offset phi in [0, beatPeriodFrames)
  double bestPhase = 0.0;
  double bestPhaseScore = -1.0;
  constexpr double kPhaseStep = 0.1;

  for (double phi = 0.0; phi < beatPeriodFrames; phi += kPhaseStep) {
    double score = 0.0;
    std::size_t beatIdx = 0;
    while (true) {
      const double framePos = phi + beatIdx * beatPeriodFrames;
      const auto frameInt = static_cast<std::size_t>(std::round(framePos));
      if (frameInt >= numFrames) break;
      score += novelty[frameInt];
      beatIdx++;
    }
    if (score > bestPhaseScore) {
      bestPhaseScore = score;
      bestPhase = phi;
    }
  }

  // 9. Find first audible beat
  std::size_t firstBeatIdx = 0;
  float maxBeatVal = 0.0f;
  std::vector<float> beatEnergies;

  for (std::size_t b = 0; ; ++b) {
    const double pos = bestPhase + b * beatPeriodFrames;
    const auto frameInt = static_cast<std::size_t>(std::round(pos));
    if (frameInt >= numFrames) break;
    const float val = novelty[frameInt];
    beatEnergies.push_back(val);
    if (val > maxBeatVal) maxBeatVal = val;
  }

  const float firstBeatThreshold = 0.15f * maxBeatVal;
  for (std::size_t b = 0; b < beatEnergies.size(); ++b) {
    if (beatEnergies[b] >= firstBeatThreshold) {
      firstBeatIdx = b;
      break;
    }
  }

  const double firstBeatFrame50Fps = bestPhase + static_cast<double>(firstBeatIdx) * beatPeriodFrames;
  const auto firstBeatFrame = static_cast<std::int64_t>(
      std::round(firstBeatFrame50Fps * (static_cast<double>(sampleRate) / 50.0)));

  // 10. Downbeat tracking (bar alignment in 4/4)
  int bestDownbeatOffset = 0;
  double bestDownbeatScore = -1.0;

  for (int offset = 0; offset < 4; ++offset) {
    double barScore = 0.0;
    for (std::size_t b = offset; b < beatEnergies.size(); b += 4) {
      barScore += beatEnergies[b];
    }
    if (barScore > bestDownbeatScore) {
      bestDownbeatScore = barScore;
      bestDownbeatOffset = offset;
    }
  }

  // 11. Build full beat frames in original sample rate
  const double samplesPerBeat = (static_cast<double>(sampleRate) * 60.0) / finalBpm;
  std::vector<std::int64_t> beatFrames;
  std::vector<std::int64_t> downbeatFrames;

  std::size_t b = 0;
  while (true) {
    const auto frame = firstBeatFrame + static_cast<std::int64_t>(std::round(b * samplesPerBeat));
    if (frame >= static_cast<std::int64_t>(numSamples)) break;
    beatFrames.push_back(frame);
    if ((static_cast<int>(b + firstBeatIdx) % 4) == bestDownbeatOffset) {
      downbeatFrames.push_back(frame);
    }
    b++;
  }

  result.bpm = finalBpm;
  result.firstBeatFrame = firstBeatFrame;
  result.beatFrames = std::move(beatFrames);
  result.downbeatFrames = std::move(downbeatFrames);
  result.gridJson = formatGridJson(result.bpm, result.firstBeatFrame, bestDownbeatOffset,
                                   sampleRate, result.beatFrames.size());
  result.success = !result.beatFrames.empty();

  return result;
}

}  // namespace zyron::analysis
