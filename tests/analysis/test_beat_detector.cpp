// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "Analysis/Bpm/BeatDetector.hpp"

using namespace zyron::analysis;

namespace {

// Helper to synthesize a click track with sharp percussive transients
std::vector<float> generateClickTrack(double bpm, int sampleRate, double durationSec,
                                      double firstBeatOffsetSec = 0.0, bool accentDownbeats = true) {
  const std::size_t totalSamples = static_cast<std::size_t>(durationSec * sampleRate);
  std::vector<float> audio(totalSamples, 0.0f);

  const double samplesPerBeat = (static_cast<double>(sampleRate) * 60.0) / bpm;
  const std::size_t firstBeatSample = static_cast<std::size_t>(firstBeatOffsetSec * sampleRate);

  std::size_t beatIdx = 0;
  while (true) {
    const std::size_t beatSample = firstBeatSample + static_cast<std::size_t>(std::round(beatIdx * samplesPerBeat));
    if (beatSample >= totalSamples) break;

    const bool isDownbeat = (beatIdx % 4 == 0);
    const float amplitude = (accentDownbeats && isDownbeat) ? 1.0f : 0.7f;
    const double clickFreq = (accentDownbeats && isDownbeat) ? 120.0 : 800.0;  // kick vs snare/click freq

    // Synthesize short percussive click burst (5 ms) with fast exponential decay
    const std::size_t burstLen = static_cast<std::size_t>(0.015 * sampleRate);
    for (std::size_t i = 0; i < burstLen && (beatSample + i) < totalSamples; ++i) {
      const double t = static_cast<double>(i) / sampleRate;
      const double env = std::exp(-t * 250.0);
      const double wave = std::cos(2.0 * 3.14159265358979323846 * clickFreq * t);
      audio[beatSample + i] += static_cast<float>(amplitude * env * wave);
    }

    beatIdx++;
  }

  return audio;
}

}  // namespace

TEST_CASE("BeatDetector tempo octave prior resolution", "[analysis][bpm]") {
  SECTION("resolves half-time and double-time to 160-180 DnB prior (§15, ADR-0010)") {
    // Standard DnB tempo: 174 BPM
    CHECK_THAT(BeatDetector::resolveTempo(174.0, TempoPriorMode::DnB), Catch::Matchers::WithinRel(174.0, 1e-4));

    // Half-time 87 BPM -> 174 BPM
    CHECK_THAT(BeatDetector::resolveTempo(87.0, TempoPriorMode::DnB), Catch::Matchers::WithinRel(174.0, 1e-4));

    // Double-time 348 BPM -> 174 BPM
    CHECK_THAT(BeatDetector::resolveTempo(348.0, TempoPriorMode::DnB), Catch::Matchers::WithinRel(174.0, 1e-4));

    // Quarter-time 43.5 BPM -> 174 BPM
    CHECK_THAT(BeatDetector::resolveTempo(43.5, TempoPriorMode::DnB), Catch::Matchers::WithinRel(174.0, 1e-4));

    // 170 BPM stays in DnB band
    CHECK_THAT(BeatDetector::resolveTempo(170.0, TempoPriorMode::DnB), Catch::Matchers::WithinRel(170.0, 1e-4));

    // 175 BPM stays in DnB band
    CHECK_THAT(BeatDetector::resolveTempo(175.0, TempoPriorMode::DnB), Catch::Matchers::WithinRel(175.0, 1e-4));
  }

  SECTION("resolves House and Techno prior to 120-132 BPM") {
    CHECK_THAT(BeatDetector::resolveTempo(128.0, TempoPriorMode::HouseTechno), Catch::Matchers::WithinRel(128.0, 1e-4));
    CHECK_THAT(BeatDetector::resolveTempo(64.0, TempoPriorMode::HouseTechno), Catch::Matchers::WithinRel(128.0, 1e-4));
    CHECK_THAT(BeatDetector::resolveTempo(256.0, TempoPriorMode::HouseTechno), Catch::Matchers::WithinRel(128.0, 1e-4));
  }

  SECTION("resolves HipHop prior to 80-115 BPM") {
    CHECK_THAT(BeatDetector::resolveTempo(90.0, TempoPriorMode::HipHop), Catch::Matchers::WithinRel(90.0, 1e-4));
    CHECK_THAT(BeatDetector::resolveTempo(180.0, TempoPriorMode::HipHop), Catch::Matchers::WithinRel(90.0, 1e-4));
    CHECK_THAT(BeatDetector::resolveTempo(45.0, TempoPriorMode::HipHop), Catch::Matchers::WithinRel(90.0, 1e-4));
  }

  SECTION("preserves default range without genre prior") {
    CHECK_THAT(BeatDetector::resolveTempo(140.0, TempoPriorMode::None), Catch::Matchers::WithinRel(140.0, 1e-4));
    CHECK_THAT(BeatDetector::resolveTempo(30.0, TempoPriorMode::None), Catch::Matchers::WithinRel(60.0, 1e-4));
    CHECK_THAT(BeatDetector::resolveTempo(240.0, TempoPriorMode::None), Catch::Matchers::WithinRel(120.0, 1e-4));
  }
}

TEST_CASE("BeatDetector peak picking (Beat This! contract: +-3 frames, > 0)", "[analysis][bpm]") {
  const std::vector<float> logits = {
      -0.5f, 0.2f, 0.8f, 0.3f, -0.1f, 0.0f, 0.1f, 0.95f, 0.4f, 0.1f, -0.2f
  };

  const auto peaks = BeatDetector::pickPeaks(logits.data(), logits.size(), 0.0f, 3);
  REQUIRE(peaks.size() == 2);
  CHECK(peaks[0] == 2);  // 0.8f is local max
  CHECK(peaks[1] == 7);  // 0.95f is local max

  SECTION("respects threshold parameter") {
    const auto highThresh = BeatDetector::pickPeaks(logits.data(), logits.size(), 0.85f, 3);
    REQUIRE(highThresh.size() == 1);
    CHECK(highThresh[0] == 7);
  }

  SECTION("handles empty and silent buffers") {
    const auto emptyPeaks = BeatDetector::pickPeaks(nullptr, 0, 0.0f, 3);
    CHECK(emptyPeaks.empty());

    const std::vector<float> negative = {-0.1f, -0.5f, -0.2f};
    const auto negPeaks = BeatDetector::pickPeaks(negative.data(), negative.size(), 0.0f, 3);
    CHECK(negPeaks.empty());
  }
}

TEST_CASE("BeatDetector buildGridFromBeats", "[analysis][bpm]") {
  // At 174 BPM, beat period at 50 FPS is 3000 / 174 = 17.241379 frames
  std::vector<std::size_t> beatFrames50;
  for (int i = 0; i < 20; ++i) {
    beatFrames50.push_back(static_cast<std::size_t>(std::round(i * (3000.0 / 174.0))));
  }

  std::vector<std::size_t> downbeatFrames50;
  for (int i = 0; i < 20; i += 4) {
    downbeatFrames50.push_back(beatFrames50[i]);
  }

  const int sampleRate = 44100;
  const std::size_t numSamples = sampleRate * 10;
  const auto grid = BeatDetector::buildGridFromBeats(beatFrames50, downbeatFrames50,
                                                    sampleRate, numSamples, TempoPriorMode::DnB);

  REQUIRE(grid.success);
  CHECK_THAT(grid.bpm, Catch::Matchers::WithinRel(174.0, 0.01));
  CHECK(grid.firstBeatFrame == 0);
  CHECK_FALSE(grid.beatFrames.empty());
  CHECK_FALSE(grid.downbeatFrames.empty());
  CHECK_FALSE(grid.gridJson.empty());
}

TEST_CASE("BeatDetector end-to-end detection on synthetic click tracks", "[analysis][bpm]") {
  BeatDetector detector;

  SECTION("detects 174.0 BPM DnB track with < 0.1 BPM accuracy at 44.1 kHz") {
    const int sampleRate = 44100;
    const double duration = 6.0;  // 6 seconds
    const auto audio = generateClickTrack(174.0, sampleRate, duration);

    const auto result = detector.detect(audio.data(), audio.size(), sampleRate, TempoPriorMode::DnB);
    REQUIRE(result.success);
    CHECK_THAT(result.bpm, Catch::Matchers::WithinAbs(174.0, 0.1));
    CHECK_FALSE(result.beatFrames.empty());
    CHECK_FALSE(result.downbeatFrames.empty());
    CHECK(result.firstBeatFrame >= 0);
    CHECK_FALSE(result.gridJson.empty());

    // Verify inter-beat sample spacing is within expected tolerance
    const double expectedSpacing = (sampleRate * 60.0) / 174.0;  // ~15206.89 samples
    REQUIRE(result.beatFrames.size() > 5);
    const double actualSpacing = static_cast<double>(result.beatFrames[1] - result.beatFrames[0]);
    CHECK_THAT(actualSpacing, Catch::Matchers::WithinAbs(expectedSpacing, 50.0));
  }

  SECTION("resolves 87.0 BPM half-time click track to 174.0 BPM via DnB prior") {
    const int sampleRate = 44100;
    const double duration = 6.0;
    const auto audio = generateClickTrack(87.0, sampleRate, duration);

    const auto result = detector.detect(audio.data(), audio.size(), sampleRate, TempoPriorMode::DnB);
    REQUIRE(result.success);
    CHECK_THAT(result.bpm, Catch::Matchers::WithinAbs(174.0, 0.15));
  }

  SECTION("detects 128.0 BPM House track at 48 kHz with HouseTechno prior") {
    const int sampleRate = 48000;
    const double duration = 8.0;
    const auto audio = generateClickTrack(128.0, sampleRate, duration);

    const auto result = detector.detect(audio.data(), audio.size(), sampleRate, TempoPriorMode::HouseTechno);
    REQUIRE(result.success);
    CHECK_THAT(result.bpm, Catch::Matchers::WithinAbs(128.0, 0.3));
  }

  SECTION("returns success = false for silence") {
    std::vector<float> silence(44100 * 2, 0.0f);
    const auto result = detector.detect(silence.data(), silence.size(), 44100, TempoPriorMode::None);
    CHECK_FALSE(result.success);
  }
}
