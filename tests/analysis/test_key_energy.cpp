// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

#include "Analysis/Energy/EnergyAnalyzer.hpp"
#include "Analysis/Key/KeyDetector.hpp"
#include "Analysis/Key/MusicalKey.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron::analysis;

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

std::vector<float> generateSineWave(float freqHz, float amplitude, float durationSec, int sampleRate) {
  const std::size_t numSamples = static_cast<std::size_t>(durationSec * static_cast<float>(sampleRate));
  std::vector<float> audio(numSamples);
  const float phaseInc = kTwoPi * freqHz / static_cast<float>(sampleRate);
  float phase = 0.0f;
  for (std::size_t i = 0; i < numSamples; ++i) {
    audio[i] = amplitude * std::sin(phase);
    phase += phaseInc;
    if (phase >= kTwoPi) phase -= kTwoPi;
  }
  return audio;
}

std::vector<float> generateMajorTriad(float rootHz, float amplitude, float durationSec, int sampleRate) {
  // Just intonation / 12-TET major triad: root, major third (+4 semitones), fifth (+7 semitones)
  const float thirdHz = rootHz * std::pow(2.0f, 4.0f / 12.0f);
  const float fifthHz = rootHz * std::pow(2.0f, 7.0f / 12.0f);

  auto r = generateSineWave(rootHz, amplitude / 3.0f, durationSec, sampleRate);
  auto t = generateSineWave(thirdHz, amplitude / 3.0f, durationSec, sampleRate);
  auto f = generateSineWave(fifthHz, amplitude / 3.0f, durationSec, sampleRate);

  for (std::size_t i = 0; i < r.size(); ++i) {
    r[i] += t[i] + f[i];
  }
  return r;
}

std::vector<float> generateMinorTriad(float rootHz, float amplitude, float durationSec, int sampleRate) {
  // Minor triad: root, minor third (+3 semitones), fifth (+7 semitones)
  const float thirdHz = rootHz * std::pow(2.0f, 3.0f / 12.0f);
  const float fifthHz = rootHz * std::pow(2.0f, 7.0f / 12.0f);

  auto r = generateSineWave(rootHz, amplitude / 3.0f, durationSec, sampleRate);
  auto t = generateSineWave(thirdHz, amplitude / 3.0f, durationSec, sampleRate);
  auto f = generateSineWave(fifthHz, amplitude / 3.0f, durationSec, sampleRate);

  for (std::size_t i = 0; i < r.size(); ++i) {
    r[i] += t[i] + f[i];
  }
  return r;
}

}  // namespace

TEST_CASE("MusicalKey Camelot conversion and validity", "[analysis][key]") {
  SECTION("Valid Camelot codes from 1A to 12B") {
    const auto kAm = MusicalKey::fromCamelot("8A");
    REQUIRE(kAm.isValid());
    CHECK(kAm.name == "Am");
    CHECK(kAm.camelot == "8A");
    CHECK(kAm.camelotNumber == 8);
    CHECK(kAm.camelotLetter == 'A');

    const auto kC = MusicalKey::fromCamelot("8B");
    REQUIRE(kC.isValid());
    CHECK(kC.name == "C");
    CHECK(kC.camelot == "8B");
    CHECK(kC.camelotNumber == 8);
    CHECK(kC.camelotLetter == 'B');

    const auto kFsm = MusicalKey::fromCamelot("11A");
    REQUIRE(kFsm.isValid());
    CHECK(kFsm.name == "F#m");

    const auto kA = MusicalKey::fromCamelot("11B");
    REQUIRE(kA.isValid());
    CHECK(kA.name == "A");

    const auto kAbm = MusicalKey::fromCamelot("1A");
    REQUIRE(kAbm.isValid());
    CHECK(kAbm.name == "Abm");

    const auto kB = MusicalKey::fromCamelot("1B");
    REQUIRE(kB.isValid());
    CHECK(kB.name == "B");
  }

  SECTION("Invalid Camelot parsing returns invalid MusicalKey") {
    CHECK_FALSE(MusicalKey::fromCamelot("").isValid());
    CHECK_FALSE(MusicalKey::fromCamelot("X").isValid());
    CHECK_FALSE(MusicalKey::fromCamelot("0A").isValid());
    CHECK_FALSE(MusicalKey::fromCamelot("13B").isValid());
    CHECK_FALSE(MusicalKey::fromCamelot("8C").isValid());
    CHECK_FALSE(MusicalKey::fromCamelot("random_string").isValid());
  }

  SECTION("Class index mapping 0..23 matches S-KEY layout") {
    // 0 = C Major (8B)
    const auto cMaj = MusicalKey::fromClassIndex(0);
    REQUIRE(cMaj.isValid());
    CHECK(cMaj.name == "C");
    CHECK(cMaj.camelot == "8B");

    // 21 = A Minor (8A)
    const auto aMin = MusicalKey::fromClassIndex(21);
    REQUIRE(aMin.isValid());
    CHECK(aMin.name == "Am");
    CHECK(aMin.camelot == "8A");

    // Out of bounds
    CHECK_FALSE(MusicalKey::fromClassIndex(-1).isValid());
    CHECK_FALSE(MusicalKey::fromClassIndex(24).isValid());
  }
}

TEST_CASE("MusicalKey harmonic compatibility and Camelot wheel rules", "[analysis][key]") {
  const auto k8A = MusicalKey::fromCamelot("8A");  // Am
  const auto k8B = MusicalKey::fromCamelot("8B");  // C
  const auto k7A = MusicalKey::fromCamelot("7A");  // Dm
  const auto k9A = MusicalKey::fromCamelot("9A");  // Em
  const auto k12A = MusicalKey::fromCamelot("12A");
  const auto k1A = MusicalKey::fromCamelot("1A");
  const auto k2A = MusicalKey::fromCamelot("2A");

  SECTION("Same key gives perfect 1.0 score") {
    CHECK_THAT(MusicalKey::compatibilityScore(k8A, k8A), WithinAbs(1.0f, 1e-4f));
    CHECK(MusicalKey::isHarmonicallyCompatible(k8A, k8A));
  }

  SECTION("Relative major and minor gives 0.9 score") {
    CHECK_THAT(MusicalKey::compatibilityScore(k8A, k8B), WithinAbs(0.9f, 1e-4f));
    CHECK_THAT(MusicalKey::compatibilityScore(k8B, k8A), WithinAbs(0.9f, 1e-4f));
    CHECK(MusicalKey::isHarmonicallyCompatible(k8A, k8B));
  }

  SECTION("Adjacent keys on wheel give 0.85 score") {
    CHECK_THAT(MusicalKey::compatibilityScore(k8A, k7A), WithinAbs(0.85f, 1e-4f));
    CHECK_THAT(MusicalKey::compatibilityScore(k8A, k9A), WithinAbs(0.85f, 1e-4f));
    CHECK(MusicalKey::isHarmonicallyCompatible(k8A, k7A));
    CHECK(MusicalKey::isHarmonicallyCompatible(k8A, k9A));
  }

  SECTION("Circular wrap-around (12A to 1A) is adjacent on Camelot wheel") {
    CHECK_THAT(MusicalKey::compatibilityScore(k12A, k1A), WithinAbs(0.85f, 1e-4f));
    CHECK_THAT(MusicalKey::compatibilityScore(k1A, k12A), WithinAbs(0.85f, 1e-4f));
    CHECK(MusicalKey::isHarmonicallyCompatible(k12A, k1A));
  }

  SECTION("Diagonal transition gives 0.70 score") {
    const auto k7B = MusicalKey::fromCamelot("7B");
    CHECK_THAT(MusicalKey::compatibilityScore(k8A, k7B), WithinAbs(0.70f, 1e-4f));
    CHECK(MusicalKey::isHarmonicallyCompatible(k8A, k7B));
  }

  SECTION("Distant keys on Camelot wheel are incompatible") {
    CHECK_THAT(MusicalKey::compatibilityScore(k8A, k2A), WithinAbs(0.0f, 1e-4f));
    CHECK_FALSE(MusicalKey::isHarmonicallyCompatible(k8A, k2A));
  }
}

TEST_CASE("KeyDetector chromagram and musical key detection", "[analysis][key]") {
  KeyDetector detector;
  constexpr int kSampleRate = 22050;

  SECTION("Empty or invalid audio returns invalid key") {
    CHECK_FALSE(detector.detectKey(nullptr, 0, kSampleRate).isValid());
    const float dummy[10] = {0};
    CHECK_FALSE(detector.detectKey(dummy, 10, kSampleRate).isValid());
  }

  SECTION("Detects C Major triad correctly") {
    // C4 (261.63 Hz), duration 1.0 sec
    auto triad = generateMajorTriad(261.63f, 0.8f, 1.0f, kSampleRate);
    const auto key = detector.detectKey(triad.data(), triad.size(), kSampleRate);

    REQUIRE(key.isValid());
    // Should detect C (8B) or a closely related harmonic key (e.g. Am 8A)
    CHECK((key.camelot == "8B" || key.camelot == "8A" || key.camelot == "7B" || key.camelot == "9B"));
    CHECK(key.confidence > 0.0f);
  }

  SECTION("Detects A Minor triad correctly") {
    // A3 (220.0 Hz), duration 1.0 sec
    auto triad = generateMinorTriad(220.0f, 0.8f, 1.0f, kSampleRate);
    const auto key = detector.detectKey(triad.data(), triad.size(), kSampleRate);

    REQUIRE(key.isValid());
    // Should detect Am (8A) or closely related harmonic key (e.g. C 8B)
    CHECK((key.camelot == "8A" || key.camelot == "8B" || key.camelot == "7A" || key.camelot == "9A"));
    CHECK(key.confidence > 0.0f);
  }

  SECTION("Chroma profile has 12 normalized bins") {
    auto triad = generateMajorTriad(261.63f, 0.8f, 1.0f, kSampleRate);
    const auto chroma = detector.computeChromaProfile(triad.data(), triad.size(), kSampleRate);

    REQUIRE(chroma.size() == 12);
    float sum = 0.0f;
    for (float v : chroma) {
      CHECK(v >= 0.0f);
      sum += v;
    }
    CHECK_THAT(sum, WithinAbs(1.0f, 0.01f));
  }
}

TEST_CASE("EnergyAnalyzer 1.0 to 10.0 scale and trajectory curve", "[analysis][energy]") {
  EnergyAnalyzer analyzer;
  constexpr int kSampleRate = 44100;

  SECTION("Silence and empty signals yield minimum energy 1.0") {
    const auto emptyRes = analyzer.analyze(nullptr, 0, kSampleRate);
    CHECK_THAT(emptyRes.globalEnergy, WithinAbs(1.0f, 1e-4f));
    CHECK(emptyRes.energyCurve.empty());

    const std::vector<float> silence(kSampleRate, 0.0f);
    const auto silRes = analyzer.analyze(silence.data(), silence.size(), kSampleRate);
    CHECK_THAT(silRes.globalEnergy, WithinAbs(1.0f, 1e-4f));
    CHECK_THAT(silRes.loudnessScore, WithinAbs(0.0f, 1e-4f));
  }

  SECTION("Low energy ambient tone yields low energy between 1.0 and 3.5") {
    // Very quiet 440 Hz tone (-34 dBFS amplitude 0.02)
    auto quietTone = generateSineWave(440.0f, 0.02f, 2.0f, kSampleRate);
    const auto res = analyzer.analyze(quietTone.data(), quietTone.size(), kSampleRate);

    CHECK(res.globalEnergy >= 1.0f);
    CHECK(res.globalEnergy <= 3.5f);
    CHECK(res.transientScore < 0.2f);
  }

  SECTION("High energy heavy beat with bass and transients yields score >= 7.5") {
    // 2 seconds of high-energy signal:
    // Heavy 55 Hz sub-bass (amplitude 0.6) + transient percussive clicks + bright noise/high frequencies
    const std::size_t numSamples = 2 * kSampleRate;
    std::vector<float> banger(numSamples, 0.0f);

    for (std::size_t i = 0; i < numSamples; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
      // Heavy sub-bass
      float sample = 0.5f * std::sin(kTwoPi * 55.0f * t);

      // Fast sharp transient clicks (simulating 174 BPM neurofunk drums: every ~7600 samples)
      if ((i % 7600) < 64) {
        sample += 0.4f * ((i % 2 == 0) ? 1.0f : -1.0f);
      }

      // Bright high frequency hats (8 kHz)
      sample += 0.2f * std::sin(kTwoPi * 8000.0f * t);
      banger[i] = std::clamp(sample, -1.0f, 1.0f);
    }

    const auto res = analyzer.analyze(banger.data(), banger.size(), kSampleRate, 0.5f);

    CHECK(res.globalEnergy >= 7.5f);
    CHECK(res.globalEnergy <= 10.0f);
    CHECK(res.loudnessScore >= 0.6f);
    CHECK(res.bassScore >= 0.5f);
    CHECK(res.transientScore >= 0.4f);
    REQUIRE_FALSE(res.energyCurve.empty());
    for (float val : res.energyCurve) {
      CHECK(val >= 1.0f);
      CHECK(val <= 10.0f);
    }
  }

  SECTION("Dynamic track shows energy progression along energy curve") {
    // Track with 1 second of quiet intro followed by 2 seconds of heavy drop
    auto quiet = generateSineWave(440.0f, 0.02f, 1.0f, kSampleRate);

    std::vector<float> drop(2 * kSampleRate);
    for (std::size_t i = 0; i < drop.size(); ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
      float sample = 0.6f * std::sin(kTwoPi * 60.0f * t);
      if ((i % 8000) < 64) sample += 0.35f;
      drop[i] = std::clamp(sample, -1.0f, 1.0f);
    }

    std::vector<float> fullTrack = quiet;
    fullTrack.insert(fullTrack.end(), drop.begin(), drop.end());

    const auto res = analyzer.analyze(fullTrack.data(), fullTrack.size(), kSampleRate, 0.5f);

    // 3 seconds total with 0.5s step -> 6 curve frames
    REQUIRE(res.energyCurve.size() >= 5);
    // Intro energy should be distinctly lower than drop energy
    CHECK(res.energyCurve.front() < res.energyCurve.back());
    // Global energy is driven by the drop (85th percentile)
    CHECK(res.globalEnergy > res.energyCurve.front());
  }

  SECTION("computeWindowEnergy single frame helper is in range 1.0..10.0") {
    const std::vector<float> silence(1024, 0.0f);
    CHECK_THAT(analyzer.computeWindowEnergy(silence.data(), silence.size(), kSampleRate),
               WithinAbs(1.0f, 1e-4f));

    auto loudChunk = generateSineWave(100.0f, 0.9f, 0.1f, kSampleRate);
    const float score = analyzer.computeWindowEnergy(loudChunk.data(), loudChunk.size(), kSampleRate);
    CHECK(score >= 4.0f);
    CHECK(score <= 10.0f);
  }
}
