// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

#include "Analysis/Energy/EnergyAnalyzer.hpp"

namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

TEST_CASE("EnergyAnalyzer: analyzeV2 master audio without stems", "[analysis][energy]") {
  const int sampleRate = 44100;
  const std::size_t numSamples = sampleRate * 10;  // 10 seconds
  std::vector<float> master(numSamples, 0.0f);

  // Generate dynamic master signal: first 5s quiet, second 5s loud bass + drum kicks
  for (std::size_t i = 0; i < numSamples; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
    if (t < 5.0) {
      master[i] = 0.05f * std::sin(2.0f * kPi * 440.0f * static_cast<float>(t));
    } else {
      const float bass = 0.6f * std::sin(2.0f * kPi * 60.0f * static_cast<float>(t));
      const double kickPhase = std::fmod(t, 0.5);
      const float kick = (kickPhase < 0.15)
                             ? (0.85f * std::exp(static_cast<float>(-kickPhase * 35.0)) *
                                std::sin(2.0f * kPi * 120.0f * static_cast<float>(t)))
                             : 0.0f;
      float sample = 0.5f * bass + 0.5f * kick;
      if ((i % 11025) < 64) {
        sample += 0.35f * ((i % 2 == 0) ? 1.0f : -1.0f);
      }
      master[i] = std::clamp(sample, -1.0f, 1.0f);
    }
  }

  zyron::analysis::EnergyAnalyzer analyzer;
  const auto result = analyzer.analyzeV2(master.data(), master.size(), sampleRate);

  CHECK_FALSE(result.hasStemAnalysis);
  CHECK(result.globalEnergy >= 1.0f);
  CHECK(result.globalEnergy <= 10.0f);
  CHECK_FALSE(result.energyCurve.empty());
  CHECK(result.loudnessScore > 0.0f);
  CHECK(result.bassScore > 0.0f);
  CHECK(result.transientScore > 0.0f);
  CHECK(result.dropIntensity > 0.2f);  // Contrast between quiet 0-5s and loud 5-10s
}

TEST_CASE("EnergyAnalyzer: analyzeV2 with separated stem buffers", "[analysis][energy]") {
  const int sampleRate = 44100;
  const std::size_t numSamples = sampleRate * 8;  // 8 seconds

  std::vector<float> master(numSamples, 0.0f);
  std::vector<float> drums(numSamples, 0.0f);
  std::vector<float> bass(numSamples, 0.0f);
  std::vector<float> vocals(numSamples, 0.0f);
  std::vector<float> other(numSamples, 0.0f);

  for (std::size_t i = 0; i < numSamples; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(sampleRate);

    // High density drums: repeated percussive impulses
    drums[i] = (std::fmod(t, 0.25) < 0.03) ? 0.9f : 0.0f;

    // Heavy bass: sustained 55 Hz sub
    bass[i] = 0.7f * std::sin(2.0f * kPi * 55.0f * static_cast<float>(t));

    // Moderate vocals in 1000 Hz range
    vocals[i] = 0.3f * std::sin(2.0f * kPi * 1000.0f * static_cast<float>(t));

    // Master is sum of stems
    master[i] = 0.35f * drums[i] + 0.35f * bass[i] + 0.20f * vocals[i];
  }

  zyron::analysis::StemBuffers stems;
  stems.drums = drums.data();
  stems.bass = bass.data();
  stems.vocals = vocals.data();
  stems.other = other.data();
  stems.numSamples = numSamples;
  stems.sampleRate = sampleRate;

  zyron::analysis::EnergyAnalyzer analyzer;
  const auto result = analyzer.analyzeV2(master.data(), master.size(), sampleRate, &stems);

  CHECK(result.hasStemAnalysis);
  CHECK(result.drumDensity > 0.3f);
  CHECK(result.bassIntensity > 0.4f);
  CHECK(result.vocalDensity > 0.2f);
  CHECK(result.globalEnergy >= 5.0f);
  CHECK(result.globalEnergy <= 10.0f);
}

TEST_CASE("EnergyAnalyzer: analyzeV2 silent stems produce minimal factor scores", "[analysis][energy]") {
  const int sampleRate = 44100;
  const std::size_t numSamples = sampleRate * 4;

  std::vector<float> silence(numSamples, 0.0f);

  zyron::analysis::StemBuffers stems;
  stems.drums = silence.data();
  stems.bass = silence.data();
  stems.vocals = silence.data();
  stems.other = silence.data();
  stems.numSamples = numSamples;
  stems.sampleRate = sampleRate;

  zyron::analysis::EnergyAnalyzer analyzer;
  const auto result = analyzer.analyzeV2(silence.data(), silence.size(), sampleRate, &stems);

  CHECK(result.hasStemAnalysis);
  CHECK(result.drumDensity == 0.0f);
  CHECK(result.bassIntensity == 0.0f);
  CHECK(result.vocalDensity == 0.0f);
  CHECK(result.globalEnergy == 1.0f);
}
