// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <vector>

#include "Analysis/Waveform/WaveformPeaks.hpp"

using namespace zyron::analysis;

namespace {

struct TempDirectory {
  std::filesystem::path path;
  TempDirectory() {
    const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("zyron_waveform_test_" + std::to_string(ts));
    std::filesystem::create_directories(path);
  }
  ~TempDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

std::vector<float> generateSineWave(float freq, float sampleRate, std::size_t numSamples, float amplitude = 0.8f) {
  std::vector<float> samples(numSamples);
  const float phaseInc = 2.0f * std::numbers::pi_v<float> * freq / sampleRate;
  float phase = 0.0f;
  for (std::size_t i = 0; i < numSamples; ++i) {
    samples[i] = amplitude * std::sin(phase);
    phase += phaseInc;
    if (phase >= 2.0f * std::numbers::pi_v<float>) {
      phase -= 2.0f * std::numbers::pi_v<float>;
    }
  }
  return samples;
}

}  // namespace

TEST_CASE("WaveformPeaks and WaveformGenerator DSP calculations", "[analysis][waveform]") {
  constexpr int kSampleRate = 44100;
  constexpr std::size_t kNumSamples = 44100;  // 1 second

  SECTION("low frequency bass tone produces dominant lowEnergy") {
    auto bassSamples = generateSineWave(60.0f, static_cast<float>(kSampleRate), kNumSamples, 0.9f);
    const float* channels[2] = {bassSamples.data(), bassSamples.data()};

    auto peaks = WaveformGenerator::generate(channels, 2, kNumSamples, kSampleRate, 256);
    REQUIRE_FALSE(peaks.detailFrames().empty());
    REQUIRE_FALSE(peaks.overviewFrames().empty());

    // Check average energies in the steady region (past initial filter transient)
    float avgLow = 0.0f;
    float avgHigh = 0.0f;
    const std::size_t start = peaks.detailFrames().size() / 4;
    const std::size_t end = peaks.detailFrames().size() * 3 / 4;
    for (std::size_t i = start; i < end; ++i) {
      avgLow += peaks.detailFrames()[i].lowEnergy;
      avgHigh += peaks.detailFrames()[i].highEnergy;
    }
    avgLow /= static_cast<float>(end - start);
    avgHigh /= static_cast<float>(end - start);

    CHECK(avgLow > 0.4f);
    CHECK(avgHigh < 0.1f);
  }

  SECTION("high frequency treble tone produces dominant highEnergy") {
    auto trebleSamples = generateSineWave(8000.0f, static_cast<float>(kSampleRate), kNumSamples, 0.9f);
    const float* channels[2] = {trebleSamples.data(), trebleSamples.data()};

    auto peaks = WaveformGenerator::generate(channels, 2, kNumSamples, kSampleRate, 256);
    REQUIRE_FALSE(peaks.detailFrames().empty());

    float avgLow = 0.0f;
    float avgHigh = 0.0f;
    const std::size_t start = peaks.detailFrames().size() / 4;
    const std::size_t end = peaks.detailFrames().size() * 3 / 4;
    for (std::size_t i = start; i < end; ++i) {
      avgLow += peaks.detailFrames()[i].lowEnergy;
      avgHigh += peaks.detailFrames()[i].highEnergy;
    }
    avgLow /= static_cast<float>(end - start);
    avgHigh /= static_cast<float>(end - start);

    CHECK(avgHigh > 0.4f);
    CHECK(avgLow < 0.1f);
  }

  SECTION("min and max amplitudes are correctly bounded") {
    auto samples = generateSineWave(440.0f, static_cast<float>(kSampleRate), kNumSamples, 0.75f);
    const float* channels[2] = {samples.data(), samples.data()};

    auto peaks = WaveformGenerator::generate(channels, 2, kNumSamples, kSampleRate, 256);
    for (const auto& f : peaks.detailFrames()) {
      CHECK(f.minLeft <= f.maxLeft);
      CHECK(f.minRight <= f.maxRight);
      CHECK(f.minLeft >= -0.8f);
      CHECK(f.maxLeft <= 0.8f);
    }
  }
}

TEST_CASE("WaveformPeaks binary serialization and deserialization", "[analysis][waveform]") {
  TempDirectory temp;
  const auto peaksPath = temp.path / "test_track.zywv";

  WaveformPeaks original;
  original.setAttributes(48000, 2, 256);

  std::vector<WaveformFrame> frames(100);
  for (std::size_t i = 0; i < frames.size(); ++i) {
    frames[i].minLeft = -0.5f + static_cast<float>(i) * 0.001f;
    frames[i].maxLeft = 0.5f - static_cast<float>(i) * 0.001f;
    frames[i].minRight = -0.4f;
    frames[i].maxRight = 0.4f;
    frames[i].lowEnergy = 0.6f;
    frames[i].midEnergy = 0.3f;
    frames[i].highEnergy = 0.1f;
  }
  original.setDetailFrames(frames);
  original.buildOverview(8);

  SECTION("save and load roundtrip preserves all frame data and metadata") {
    std::string err;
    REQUIRE(original.saveToFile(peaksPath, &err));

    auto loadedOpt = WaveformPeaks::loadFromFile(peaksPath, &err);
    REQUIRE(loadedOpt.has_value());

    const auto& loaded = *loadedOpt;
    CHECK(loaded.sampleRate() == 48000);
    CHECK(loaded.channels() == 2);
    CHECK(loaded.samplesPerFrame() == 256);
    REQUIRE(loaded.detailFrames().size() == original.detailFrames().size());
    REQUIRE(loaded.overviewFrames().size() == original.overviewFrames().size());

    CHECK_THAT(loaded.detailFrames()[0].minLeft, Catch::Matchers::WithinRel(-0.5f, 1e-4f));
    CHECK_THAT(loaded.detailFrames()[0].maxLeft, Catch::Matchers::WithinRel(0.5f, 1e-4f));
    CHECK_THAT(loaded.detailFrames()[0].lowEnergy, Catch::Matchers::WithinRel(0.6f, 1e-4f));
  }

  SECTION("loadFromFile gracefully rejects invalid files") {
    const auto invalidPath = temp.path / "corrupt.zywv";
    {
      std::ofstream f(invalidPath, std::ios::binary);
      f << "NOT_A_VALID_HEADER";
    }

    std::string err;
    auto result = WaveformPeaks::loadFromFile(invalidPath, &err);
    CHECK_FALSE(result.has_value());
    CHECK_FALSE(err.empty());
  }
}
