// SPDX-License-Identifier: AGPL-3.0-only
//
// The Beat This! host pipeline (feature contract, 1500-frame windows, aggregation, grid fit) with a fake network, so
// it runs without the weights: the fake "beat" output follows the loudness of the log-mel frames, which for a click
// track puts a peak on every click. The real network is exercised by tests/ai/test_onnx_models.cpp and the app tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Analysis/Bpm/BeatThisPipeline.hpp"

using namespace zyron::analysis;
using Catch::Matchers::WithinAbs;

namespace {

constexpr int kRate = 44100;

/// Clicks every `periodSeconds`, starting at `firstSeconds`; silence in [gapStart, gapEnd) seconds (a break).
std::vector<float> clickTrack(double seconds, double periodSeconds, double firstSeconds, double gapStart = -1.0,
                              double gapEnd = -1.0) {
  std::vector<float> audio(static_cast<std::size_t>(seconds * kRate), 0.0F);
  for (double t = firstSeconds; t < seconds; t += periodSeconds) {
    if (t >= gapStart && t < gapEnd) {
      continue;
    }
    const auto start = static_cast<std::size_t>(t * kRate);
    for (std::size_t i = 0; i < 400 && start + i < audio.size(); ++i) {
      audio[start + i] = 0.8F * std::sin(0.4F * static_cast<float>(i)) * std::exp(-static_cast<float>(i) / 80.0F);
    }
  }
  return audio;
}

/// Every FFT bin feeds mel bin (bin / 4): enough for a loudness-following fake network.
std::vector<float> flatFilterbank() {
  std::vector<float> fb(static_cast<std::size_t>(BeatThisPipeline::kFftBins) * BeatThisPipeline::kMelBins, 0.0F);
  for (int k = 0; k < BeatThisPipeline::kFftBins; ++k) {
    fb[static_cast<std::size_t>(k) * BeatThisPipeline::kMelBins + static_cast<std::size_t>(std::min(k / 4, 127))] = 0.02F;
  }
  return fb;
}

/// Fake network: a frame is a beat when its summed log-mel energy stands out from the window's median.
bool fakeNetwork(const float* spect, float* beat, float* downbeat) {
  std::vector<float> energy(BeatThisPipeline::kChunkFrames, 0.0F);
  for (int t = 0; t < BeatThisPipeline::kChunkFrames; ++t) {
    for (int m = 0; m < BeatThisPipeline::kMelBins; ++m) {
      energy[static_cast<std::size_t>(t)] += spect[t * BeatThisPipeline::kMelBins + m];
    }
  }
  std::vector<float> sorted = energy;
  std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
  const float median = sorted[sorted.size() / 2];
  float maximum = 0.0F;
  for (const float e : energy) {
    maximum = std::max(maximum, e);
  }
  const float threshold = median + 0.5F * (maximum - median);
  for (int t = 0; t < BeatThisPipeline::kChunkFrames; ++t) {
    beat[t] = energy[static_cast<std::size_t>(t)] > threshold ? 5.0F : -5.0F;
    downbeat[t] = -5.0F;
  }
  return true;
}

}  // namespace

TEST_CASE("BeatThisPipeline: log-mel features follow the model contract", "[analysis][beat_this]") {
  const BeatThisPipeline pipeline(flatFilterbank(), fakeNetwork);

  SECTION("one 20 ms frame per 441 samples at 22.05 kHz, 128 mel bins") {
    const std::vector<float> audio(22050, 0.0F);  // one second of silence
    const auto features = pipeline.logMel(audio.data(), audio.size());
    CHECK(features.size() == static_cast<std::size_t>((22050 / 441 + 1) * BeatThisPipeline::kMelBins));
    for (const float value : features) {
      CHECK(value == 0.0F);  // log1p(0)
    }
  }

  SECTION("a wrong filterbank size gives no features instead of garbage") {
    const BeatThisPipeline broken(std::vector<float>(10, 0.0F), fakeNetwork);
    const std::vector<float> audio(22050, 0.1F);
    CHECK(broken.logMel(audio.data(), audio.size()).empty());
  }
}

TEST_CASE("BeatThisPipeline: tempo and phase over several windows", "[analysis][beat_this]") {
  const BeatThisPipeline pipeline(flatFilterbank(), fakeNetwork);

  SECTION("a steady 150 BPM track longer than two windows") {
    const auto audio = clickTrack(100.0, 0.4, 0.5);  // 5000 frames: four windows
    std::string error;
    const auto result = pipeline.analyze(audio.data(), audio.size(), kRate, TempoPriorMode::None, &error);
    INFO(error);
    REQUIRE(result.success);
    CHECK_THAT(result.bpm, WithinAbs(150.0, 0.5));
    // The anchor is a click: its time is 0.5 s plus a whole number of 0.4 s periods.
    const double anchor = static_cast<double>(result.firstBeatFrame) / kRate;
    const double beats = (anchor - 0.5) / 0.4;
    CHECK_THAT(beats - std::round(beats), WithinAbs(0.0, 0.05));
  }

  SECTION("a break without beats does not distort the tempo (the grid is fitted, not counted)") {
    const auto audio = clickTrack(100.0, 0.4, 0.5, 30.0, 52.0);
    const auto result = pipeline.analyze(audio.data(), audio.size(), kRate, TempoPriorMode::None);
    REQUIRE(result.success);
    CHECK_THAT(result.bpm, WithinAbs(150.0, 0.5));
  }

  SECTION("silence is reported as no beat") {
    const std::vector<float> audio(static_cast<std::size_t>(20 * kRate), 0.0F);
    std::string error;
    const auto result = pipeline.analyze(audio.data(), audio.size(), kRate, TempoPriorMode::None, &error);
    CHECK_FALSE(result.success);
    CHECK_FALSE(error.empty());
  }
}

TEST_CASE("BeatThisPipeline: a failing network is reported", "[analysis][beat_this]") {
  const BeatThisPipeline pipeline(flatFilterbank(), [](const float*, float*, float*) { return false; });
  const auto audio = clickTrack(20.0, 0.4, 0.5);
  std::string error;
  const auto result = pipeline.analyze(audio.data(), audio.size(), kRate, TempoPriorMode::None, &error);
  CHECK_FALSE(result.success);
  CHECK_FALSE(error.empty());
}
