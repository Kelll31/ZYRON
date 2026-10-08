// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Bpm/BeatThisPipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "Analysis/Bpm/Beatgrid.hpp"
#include "Analysis/Features/Resampler.hpp"
#include "Analysis/Features/Stft.hpp"

namespace zyron::analysis {

namespace {

constexpr float kLogMultiplier = 1000.0F;
constexpr int kPeakRadius = 3;  // peakKernel 7 in the model config
constexpr std::size_t kDeduplicateWidth = 1;
constexpr float kPeakThreshold = 0.0F;
constexpr float kNoPrediction = -1000.0F;

/// Merges peaks that are at most `width` frames apart, keeping the first of each group.
std::vector<std::size_t> deduplicate(const std::vector<std::size_t>& peaks, std::size_t width) {
  std::vector<std::size_t> merged;
  for (const std::size_t peak : peaks) {
    if (merged.empty() || peak - merged.back() > width) {
      merged.push_back(peak);
    }
  }
  return merged;
}

/// The beat nearest to `frame` (`beats` is sorted and not empty).
std::size_t nearestBeat(const std::vector<std::size_t>& beats, std::size_t frame) {
  const auto after = std::lower_bound(beats.begin(), beats.end(), frame);
  if (after == beats.end()) {
    return beats.back();
  }
  if (after == beats.begin()) {
    return *after;
  }
  const std::size_t before = *(after - 1);
  return (frame - before <= *after - frame) ? before : *after;
}

/// Snaps every downbeat to the nearest beat (the reference's postprocessing), dropping duplicates.
std::vector<std::size_t> snapToBeats(const std::vector<std::size_t>& downbeats, const std::vector<std::size_t>& beats) {
  std::vector<std::size_t> snapped;
  if (beats.empty()) {
    return snapped;
  }
  for (const std::size_t downbeat : downbeats) {
    const std::size_t beat = nearestBeat(beats, downbeat);
    if (snapped.empty() || snapped.back() != beat) {
      snapped.push_back(beat);
    }
  }
  return snapped;
}


/// Robust beat grid through the network's beats. The beats of a real track have gaps (breaks, intros without drums)
/// and the odd extra peak, so the index of a beat is not its position in the list: the tempo comes from the typical
/// gap, every beat is given the integer index nearest to its time, and a least-squares line through (index, time) -
/// with outliers dropped - gives the period and a phase that holds over the whole track.
BeatDetectionResult fitGrid(const std::vector<std::size_t>& beats, const std::vector<std::size_t>& downbeats,
                            int sampleRate, std::size_t numSamples, TempoPriorMode prior) {
  BeatDetectionResult result;
  constexpr double kFps = BeatThisPipeline::kFramesPerSecond;
  constexpr double kMinGap = 10.0;  // frames: 300 BPM
  constexpr double kMaxGap = 60.0;  // frames: 50 BPM
  constexpr double kInlier = 0.15;  // of a period
  if (beats.size() < 4 || sampleRate <= 0) {
    return result;
  }

  std::vector<double> gaps;
  for (std::size_t i = 1; i < beats.size(); ++i) {
    const double gap = static_cast<double>(beats[i] - beats[i - 1]);
    if (gap >= kMinGap && gap <= kMaxGap) {
      gaps.push_back(gap);
    }
  }
  if (gaps.size() < 3) {
    return result;
  }
  std::nth_element(gaps.begin(), gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2), gaps.end());
  const double median = gaps[gaps.size() / 2];

  // First estimate: the mean of the ordinary gaps (frames are whole numbers, so the median alone is too coarse).
  double sum = 0.0;
  int count = 0;
  for (const double gap : gaps) {
    if (std::abs(gap - median) <= 1.5) {
      sum += gap;
      ++count;
    }
  }
  double period = count > 0 ? sum / count : median;  // frames per beat

  std::vector<double> index(beats.size(), 0.0);
  std::vector<bool> used(beats.size(), true);
  double origin = static_cast<double>(beats.front());
  for (int iteration = 0; iteration < 4; ++iteration) {
    // Beat numbers follow the gaps one by one, so a wrong period does not pile up and a missed beat just skips a number.
    std::size_t previous = 0;
    index[0] = 0.0;
    used.assign(beats.size(), false);
    used[0] = true;
    for (std::size_t i = 1; i < beats.size(); ++i) {
      const double gap = static_cast<double>(beats[i] - beats[previous]);
      if (gap < 0.6 * period) {
        continue;  // an extra peak between two beats
      }
      index[i] = index[previous] + std::max(1.0, std::round(gap / period));
      used[i] = true;
      previous = i;
    }

    double n = 0.0, sx = 0.0, sy = 0.0, sxy = 0.0, sxx = 0.0;
    for (int pass = 0; pass < 2; ++pass) {
      n = sx = sy = sxy = sxx = 0.0;
      for (std::size_t i = 0; i < beats.size(); ++i) {
        if (!used[i]) {
          continue;
        }
        const double x = index[i];
        const double y = static_cast<double>(beats[i]);
        n += 1.0;
        sx += x;
        sy += y;
        sxy += x * y;
        sxx += x * x;
      }
      const double denominator = n * sxx - sx * sx;
      if (n < 4.0 || denominator <= 0.0) {
        return result;
      }
      period = (n * sxy - sx * sy) / denominator;
      origin = (sy - period * sx) / n;
      if (pass == 0) {  // drop the beats that sit far from the fitted line, then fit again
        for (std::size_t i = 0; i < beats.size(); ++i) {
          if (used[i] && std::abs(static_cast<double>(beats[i]) - (origin + index[i] * period)) > kInlier * period) {
            used[i] = false;
          }
        }
      }
    }
    if (period < kMinGap || period > kMaxGap) {
      return result;
    }
  }

  const double rawBpm = 60.0 * kFps / period;
  const double bpm = BeatDetector::resolveTempo(rawBpm, prior);
  if (bpm <= 0.0) {
    return result;
  }

  // Phase of the downbeat: which of four consecutive beats the network calls the first of a bar.
  int downbeatOffset = 0;
  if (!downbeats.empty()) {
    std::array<int, 4> votes{};
    for (const std::size_t downbeat : downbeats) {
      const long long idx = std::llround((static_cast<double>(downbeat) - origin) / period);
      ++votes[static_cast<std::size_t>(((idx % 4) + 4) % 4)];
    }
    downbeatOffset = static_cast<int>(std::max_element(votes.begin(), votes.end()) - votes.begin());
  }

  // Anchor the grid at a beat in the middle of the track: a slightly wrong tempo then costs the least where it matters.
  const double samplesPerBeat = static_cast<double>(sampleRate) * 60.0 / bpm;
  const double middleSample = static_cast<double>(numSamples) / 2.0;
  const double originSample = origin * static_cast<double>(sampleRate) / kFps;
  const double beatsToMiddle = std::round((middleSample - originSample) / samplesPerBeat);
  const auto firstBeat = static_cast<std::int64_t>(std::llround(originSample + beatsToMiddle * samplesPerBeat));

  result.bpm = bpm;
  result.firstBeatFrame = firstBeat;
  const auto stride = static_cast<std::int64_t>(std::llround(samplesPerBeat));
  for (std::int64_t frame = firstBeat % stride; frame < static_cast<std::int64_t>(numSamples); frame += stride) {
    result.beatFrames.push_back(frame);
  }
  BeatgridData grid;
  grid.bpm = bpm;
  grid.firstBeatFrame = firstBeat;
  grid.sampleRate = sampleRate;
  grid.downbeatOffset = downbeatOffset;
  grid.source = "auto";
  result.gridJson = grid.toJson();
  result.success = true;
  return result;
}

}  // namespace

BeatThisPipeline::BeatThisPipeline(std::vector<float> melFilterbank, BeatThisInference inference)
    : melFilterbank_(std::move(melFilterbank)), inference_(std::move(inference)) {}

std::vector<float> BeatThisPipeline::logMel(const float* mono22k, std::size_t numSamples) const {
  if (mono22k == nullptr || numSamples == 0 ||
      melFilterbank_.size() != static_cast<std::size_t>(kFftBins) * kMelBins) {
    return {};
  }
  StftConfig config;
  config.nFft = 1024;
  config.hopLength = 441;
  config.windowType = WindowType::HannPeriodic;
  config.center = true;
  config.scaleBySqrtN = true;
  const auto magnitude = Stft(config).processMagnitude(mono22k, numSamples);

  std::vector<float> features(magnitude.size() * kMelBins, 0.0F);
  for (std::size_t t = 0; t < magnitude.size(); ++t) {
    float* row = features.data() + t * kMelBins;
    const std::vector<float>& bins = magnitude[t];
    for (int k = 0; k < kFftBins; ++k) {
      const float value = bins[static_cast<std::size_t>(k)];
      if (value == 0.0F) {
        continue;
      }
      const float* filter = melFilterbank_.data() + static_cast<std::size_t>(k) * kMelBins;
      for (int m = 0; m < kMelBins; ++m) {
        row[m] += value * filter[m];
      }
    }
    for (int m = 0; m < kMelBins; ++m) {
      row[m] = std::log1p(kLogMultiplier * row[m]);
    }
  }
  return features;
}

bool BeatThisPipeline::predict(const std::vector<float>& features, std::vector<float>& beatLogits,
                               std::vector<float>& downbeatLogits) const {
  const std::size_t frames = features.size() / kMelBins;
  beatLogits.assign(frames, kNoPrediction);
  downbeatLogits.assign(frames, kNoPrediction);
  if (frames == 0 || !inference_) {
    return false;
  }

  // The reference pads every piece with a border of zero frames on both sides, so each real frame sits in the interior
  // of some window (the interior is the window without its borders).
  const std::size_t padded = frames + 2 * kBorderFrames;
  const std::size_t interior = kChunkFrames - 2 * kBorderFrames;
  std::vector<std::size_t> starts;
  for (std::size_t start = 0; start + kChunkFrames < padded; start += interior) {
    starts.push_back(start);
  }
  // The last window is moved back so that it ends exactly at the end of the (padded) piece.
  starts.push_back(padded > static_cast<std::size_t>(kChunkFrames) ? padded - kChunkFrames : 0);

  std::vector<float> window(static_cast<std::size_t>(kChunkFrames) * kMelBins);
  std::vector<float> beat(kChunkFrames);
  std::vector<float> downbeat(kChunkFrames);

  // Walking the windows backwards and overwriting makes the earlier window win where two overlap ("keep_first").
  for (auto it = starts.rbegin(); it != starts.rend(); ++it) {
    const std::size_t start = *it;
    std::fill(window.begin(), window.end(), 0.0F);
    for (std::size_t j = 0; j < static_cast<std::size_t>(kChunkFrames); ++j) {
      const std::size_t p = start + j;  // position in the padded piece
      if (p >= kBorderFrames && p - kBorderFrames < frames) {
        std::copy_n(features.data() + (p - kBorderFrames) * kMelBins, kMelBins, window.data() + j * kMelBins);
      }
    }
    if (!inference_(window.data(), beat.data(), downbeat.data())) {
      return false;
    }
    for (std::size_t j = kBorderFrames; j < static_cast<std::size_t>(kChunkFrames - kBorderFrames); ++j) {
      const std::size_t p = start + j;
      if (p >= kBorderFrames && p - kBorderFrames < frames) {
        beatLogits[p - kBorderFrames] = beat[j];
        downbeatLogits[p - kBorderFrames] = downbeat[j];
      }
    }
  }
  return true;
}

BeatDetectionResult BeatThisPipeline::analyze(const float* mono, std::size_t numSamples, int sampleRate,
                                              TempoPriorMode prior, std::string* error) const {
  const auto fail = [&](const char* message) {
    if (error != nullptr) {
      *error = message;
    }
    return BeatDetectionResult{};
  };
  if (mono == nullptr || numSamples == 0 || sampleRate <= 0) {
    return fail("no audio");
  }

  const std::vector<float> resampled =
      sampleRate == kSampleRate ? std::vector<float>(mono, mono + numSamples)
                                : AudioResampler::resample(mono, numSamples, sampleRate, kSampleRate);
  const std::vector<float> features = logMel(resampled.data(), resampled.size());
  if (features.empty()) {
    return fail("could not compute the spectrogram (is the mel filterbank loaded?)");
  }

  std::vector<float> beatLogits;
  std::vector<float> downbeatLogits;
  if (!predict(features, beatLogits, downbeatLogits)) {
    return fail("the Beat This! network failed to run");
  }

  const std::size_t frames = beatLogits.size();
  const auto beats = deduplicate(BeatDetector::pickPeaks(beatLogits.data(), frames, kPeakThreshold, kPeakRadius),
                                 kDeduplicateWidth);
  const auto downbeats = snapToBeats(
      deduplicate(BeatDetector::pickPeaks(downbeatLogits.data(), frames, kPeakThreshold, kPeakRadius),
                  kDeduplicateWidth),
      beats);

  BeatDetectionResult result = fitGrid(beats, downbeats, sampleRate, numSamples, prior);
  if (!result.success) {
    return fail("the network found no steady beat");
  }
  return result;
}

}  // namespace zyron::analysis
