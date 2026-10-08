// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Structure/VocalDetector.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "Analysis/Features/Fft.hpp"

namespace zyron::analysis {

namespace {

constexpr double kTargetRateHz = 11025.0;
constexpr std::size_t kFrameSize = 1024;
constexpr std::size_t kHopSize = 512;
constexpr std::size_t kCepstrumSize = 512;
constexpr double kSpectrumLowHz = 100.0;  // log-spectrum range the comb is looked for in
constexpr double kSpectrumHighHz = 4000.0;
constexpr double kBandLowHz = 300.0;  // the voice band (centroid and energy)
constexpr double kBandHighHz = 3400.0;
constexpr std::size_t kTrendBins = 41;       // moving average removed from the log spectrum (the envelope)
constexpr std::size_t kQuefrencyLow = 14;    // 400 Hz fundamental = comb spacing of 37 bins -> 512 / 37
constexpr std::size_t kQuefrencyHigh = 69;   // 80 Hz
constexpr std::size_t kQuefrencyMeanLow = 8;
constexpr std::size_t kQuefrencyMeanHigh = 128;
constexpr double kQuietBandShare = 0.1;  // of the 75th percentile of the frames' band energy
constexpr double kStemFloorRms = 0.003;  // about -50 dBFS
constexpr double kStemLoudShare = 0.15;  // of the stem's 90th percentile bar level
constexpr double kEps = 1.0e-9;

struct FrameFeatures {
  bool voiced{false};
  bool audible{false};
  double centroidHz{0.0};
};

double percentile(std::vector<double> values, double share) {
  if (values.empty()) {
    return 0.0;
  }
  const auto index = static_cast<std::size_t>(share * static_cast<double>(values.size() - 1));
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
  return values[index];
}

/// Low-passes (two cascaded box filters of the decimation length) and keeps every `factor`-th sample.
std::vector<float> decimate(const float* in, std::size_t frames, std::size_t factor) {
  if (factor <= 1) {
    return std::vector<float>(in, in + frames);
  }
  const auto box = [&](const float* src, float* dst) {
    double sum = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
      sum += src[i];
      if (i >= factor) {
        sum -= src[i - factor];
      }
      dst[i] = static_cast<float>(sum / static_cast<double>(factor));
    }
  };
  std::vector<float> first(frames);
  std::vector<float> second(frames);
  box(in, first.data());
  box(first.data(), second.data());
  std::vector<float> out;
  out.reserve(frames / factor + 1);
  for (std::size_t i = factor; i < frames; i += factor) {
    out.push_back(second[i]);
  }
  return out;
}

std::vector<VocalRegion> regionsFromBars(std::vector<bool> flags, const VocalGrid& grid) {
  const int bars = static_cast<int>(flags.size());
  // Bridge short gaps between vocal bars.
  for (int i = 0; i < bars;) {
    if (flags[static_cast<std::size_t>(i)]) {
      ++i;
      continue;
    }
    int j = i;
    while (j < bars && !flags[static_cast<std::size_t>(j)]) {
      ++j;
    }
    if (i > 0 && j < bars && j - i <= kBridgeGapBars) {
      for (int k = i; k < j; ++k) {
        flags[static_cast<std::size_t>(k)] = true;
      }
    }
    i = j;
  }
  std::vector<VocalRegion> regions;
  for (int i = 0; i < bars;) {
    if (!flags[static_cast<std::size_t>(i)]) {
      ++i;
      continue;
    }
    int j = i;
    while (j < bars && flags[static_cast<std::size_t>(j)]) {
      ++j;
    }
    if (j - i >= kMinRegionBars) {
      regions.push_back({grid.originSec + i * grid.barSec, grid.originSec + j * grid.barSec});
    }
    i = j;
  }
  return regions;
}

/// Cepstral prominence (peak over mean) of the detrended log spectrum of one frame, and its band centroid.
class FrameAnalyzer {
 public:
  FrameAnalyzer(double rate)
      : fft_(kFrameSize), cepstrumFft_(kCepstrumSize), window_(kFrameSize), buffer_(kFrameSize),
        spectrum_(kFrameSize / 2 + 1), logSpec_(kCepstrumSize), cepstrum_(kCepstrumSize / 2 + 1) {
    binHz_ = rate / static_cast<double>(kFrameSize);
    specLow_ = bin(kSpectrumLowHz);
    specHigh_ = std::min(bin(kSpectrumHighHz), kFrameSize / 2);
    bandLow_ = bin(kBandLowHz);
    bandHigh_ = std::min(bin(kBandHighHz), kFrameSize / 2);
    span_ = specHigh_ - specLow_ + 1;
    prefix_.resize(span_ + 1);
    for (std::size_t i = 0; i < kFrameSize; ++i) {
      window_[i] = 0.5F - 0.5F * std::cos(2.0F * std::numbers::pi_v<float> * static_cast<float>(i) /
                                          static_cast<float>(kFrameSize));
    }
  }

  /// Fills the frame's features; returns the voice-band energy.
  double analyze(const float* frame, FrameFeatures& out) {
    for (std::size_t i = 0; i < kFrameSize; ++i) {
      buffer_[i] = frame[i] * window_[i];
    }
    fft_.forwardReal(buffer_.data(), spectrum_.data());
    double energy = 0.0;
    double weighted = 0.0;
    for (std::size_t k = bandLow_; k <= bandHigh_; ++k) {
      const double m = std::abs(spectrum_[k]);
      energy += m * m;
      weighted += m * m * static_cast<double>(k);
    }
    out.centroidHz = energy > kEps ? weighted / energy * binHz_ : 0.0;
    out.voiced = prominence() >= kMinCepstralProminence;
    return energy;
  }

 private:
  [[nodiscard]] std::size_t bin(double hz) const { return static_cast<std::size_t>(std::lround(hz / binHz_)); }

  double prominence() {
    std::fill(logSpec_.begin(), logSpec_.end(), 0.0F);
    prefix_[0] = 0.0;
    for (std::size_t i = 0; i < span_; ++i) {
      const double l = std::log(static_cast<double>(std::abs(spectrum_[specLow_ + i])) + 1.0e-6);
      logSpec_[i] = static_cast<float>(l);
      prefix_[i + 1] = prefix_[i] + l;
    }
    for (std::size_t i = 0; i < span_; ++i) {
      const std::size_t lo = i >= kTrendBins / 2 ? i - kTrendBins / 2 : 0;
      const std::size_t hi = std::min(span_, i + kTrendBins / 2 + 1);
      logSpec_[i] -= static_cast<float>((prefix_[hi] - prefix_[lo]) / static_cast<double>(hi - lo));
    }
    cepstrumFft_.forwardReal(logSpec_.data(), cepstrum_.data());
    double peak = 0.0;
    double sum = 0.0;
    for (std::size_t q = kQuefrencyMeanLow; q <= kQuefrencyMeanHigh; ++q) {
      const double m = std::abs(cepstrum_[q]);
      sum += m;
      if (q >= kQuefrencyLow && q <= kQuefrencyHigh) {
        peak = std::max(peak, m);
      }
    }
    const double mean = sum / static_cast<double>(kQuefrencyMeanHigh - kQuefrencyMeanLow + 1);
    return mean > kEps ? peak / mean : 0.0;
  }

  Fft fft_;
  Fft cepstrumFft_;
  std::vector<float> window_;
  std::vector<float> buffer_;
  std::vector<std::complex<float>> spectrum_;
  std::vector<float> logSpec_;
  std::vector<std::complex<float>> cepstrum_;
  std::vector<double> prefix_;
  double binHz_{1.0};
  std::size_t specLow_{0}, specHigh_{0}, bandLow_{0}, bandHigh_{0}, span_{1};
};

}  // namespace

std::vector<VocalRegion> detectVocals(const float* mono, std::size_t frames, int sampleRate, const VocalGrid& grid) {
  if (mono == nullptr || frames == 0 || sampleRate <= 0 || grid.bars <= 0 || grid.barSec <= 0.0) {
    return {};
  }
  const std::size_t factor =
      std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(sampleRate / kTargetRateHz)));
  const double rate = static_cast<double>(sampleRate) / static_cast<double>(factor);
  const std::vector<float> audio = decimate(mono, frames, factor);
  if (audio.size() < kFrameSize) {
    return {};
  }

  const std::size_t frameCount = (audio.size() - kFrameSize) / kHopSize + 1;
  std::vector<FrameFeatures> features(frameCount);
  std::vector<double> bandEnergy(frameCount, 0.0);
  FrameAnalyzer analyzer(rate);
  for (std::size_t f = 0; f < frameCount; ++f) {
    bandEnergy[f] = analyzer.analyze(audio.data() + f * kHopSize, features[f]);
  }
  const double quiet = kQuietBandShare * percentile(bandEnergy, 0.75);
  for (std::size_t f = 0; f < frameCount; ++f) {
    features[f].audible = bandEnergy[f] > quiet && bandEnergy[f] > kEps;
  }

  // Per bar: the share of voiced frames and how much the centroid moves among them.
  const double hopSec = static_cast<double>(kHopSize) / rate;
  const double frameSec = static_cast<double>(kFrameSize) / rate;
  std::vector<bool> flags(static_cast<std::size_t>(grid.bars), false);
  for (int bar = 0; bar < grid.bars; ++bar) {
    const double t0 = grid.originSec + bar * grid.barSec;
    const double t1 = t0 + grid.barSec;
    const auto first = static_cast<std::size_t>(std::max(0.0, std::ceil((t0 - frameSec / 2.0) / hopSec)));
    const auto last =
        std::min(frameCount, static_cast<std::size_t>(std::max(0.0, std::ceil((t1 - frameSec / 2.0) / hopSec))));
    std::size_t total = 0;
    std::size_t voiced = 0;
    double sum = 0.0;
    double sumSquares = 0.0;
    for (std::size_t f = first; f < last; ++f) {
      ++total;
      if (features[f].voiced && features[f].audible) {
        ++voiced;
        sum += features[f].centroidHz;
        sumSquares += features[f].centroidHz * features[f].centroidHz;
      }
    }
    if (total == 0 || voiced < 3 || static_cast<double>(voiced) < kMinVoicedShare * static_cast<double>(total)) {
      continue;
    }
    const double mean = sum / static_cast<double>(voiced);
    const double variance = std::max(0.0, sumSquares / static_cast<double>(voiced) - mean * mean);
    flags[static_cast<std::size_t>(bar)] = mean > kEps && std::sqrt(variance) / mean >= kMinCentroidVariation;
  }
  return regionsFromBars(std::move(flags), grid);
}

std::vector<VocalRegion> vocalsFromStem(const float* vocalMono, std::size_t frames, int sampleRate,
                                        const VocalGrid& grid) {
  if (vocalMono == nullptr || frames == 0 || sampleRate <= 0 || grid.bars <= 0 || grid.barSec <= 0.0) {
    return {};
  }
  const double rate = static_cast<double>(sampleRate);
  std::vector<double> rms(static_cast<std::size_t>(grid.bars), 0.0);
  for (int bar = 0; bar < grid.bars; ++bar) {
    const auto a = static_cast<std::size_t>(std::max(0.0, (grid.originSec + bar * grid.barSec) * rate));
    const auto b = std::min(
        frames, static_cast<std::size_t>(std::max(0.0, (grid.originSec + (bar + 1) * grid.barSec) * rate)));
    if (b <= a) {
      continue;
    }
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i) {
      sum += static_cast<double>(vocalMono[i]) * vocalMono[i];
    }
    rms[static_cast<std::size_t>(bar)] = std::sqrt(sum / static_cast<double>(b - a));
  }
  const double loud = percentile(rms, 0.9);
  std::vector<bool> flags(rms.size(), false);
  for (std::size_t i = 0; i < rms.size(); ++i) {
    flags[i] = rms[i] >= kStemFloorRms && rms[i] >= kStemLoudShare * loud;
  }
  return regionsFromBars(std::move(flags), grid);
}

}  // namespace zyron::analysis
