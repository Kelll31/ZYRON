// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "Analysis/Chord/ChordAnalyzer.hpp"
#include "Analysis/Chord/ChordTypes.hpp"

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Synthesizes a chord signal with specific harmonic frequencies
std::vector<float> generateChordAudio(const std::vector<float>& freqs, int sampleRate, double durationSec) {
  const std::size_t numSamples = static_cast<std::size_t>(durationSec * sampleRate);
  std::vector<float> audio(numSamples, 0.0f);

  for (std::size_t i = 0; i < numSamples; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
    float sample = 0.0f;
    for (float f : freqs) {
      sample += std::sin(2.0f * kPi * f * static_cast<float>(t));
    }
    audio[i] = sample / static_cast<float>(freqs.size());
  }

  return audio;
}

}  // namespace

TEST_CASE("ChordAnalyzer: 170-class index translation contract (docs/AI_MODELS.md)", "[analysis][chord]") {
  // Class 0 = Silence / No chord
  const auto silentChord = zyron::analysis::ChordAnalyzer::classIndexToChord(0);
  CHECK(silentChord.isSilent());
  CHECK(silentChord.name == "N");
  CHECK(zyron::analysis::ChordAnalyzer::chordToClassIndex(silentChord) == 0);

  // Class 1 = C Major (root 0, quality Major)
  const auto cMaj = zyron::analysis::ChordAnalyzer::classIndexToChord(1);
  CHECK(cMaj.rootNote == 0);
  CHECK(cMaj.quality == zyron::analysis::ChordQuality::Major);
  CHECK(cMaj.name == "C");
  CHECK(zyron::analysis::ChordAnalyzer::chordToClassIndex(cMaj) == 1);

  // Class 13 = C Minor (root 0, quality Minor)
  const auto cMin = zyron::analysis::ChordAnalyzer::classIndexToChord(13);
  CHECK(cMin.rootNote == 0);
  CHECK(cMin.quality == zyron::analysis::ChordQuality::Minor);
  CHECK(cMin.name == "Cm");
  CHECK(zyron::analysis::ChordAnalyzer::chordToClassIndex(cMin) == 13);

  // Class 22 = A Minor: 1 + 1*12 + 9 = 22
  const auto aMin = zyron::analysis::ChordAnalyzer::classIndexToChord(22);
  CHECK(aMin.rootNote == 9);
  CHECK(aMin.quality == zyron::analysis::ChordQuality::Minor);
  CHECK(aMin.name == "Am");
  CHECK(zyron::analysis::ChordAnalyzer::chordToClassIndex(aMin) == 22);

  // Roundtrip validation across all valid classes (1..168)
  for (int c = 1; c <= 168; ++c) {
    const auto chord = zyron::analysis::ChordAnalyzer::classIndexToChord(c);
    const int roundtrip = zyron::analysis::ChordAnalyzer::chordToClassIndex(chord);
    CHECK(roundtrip == c);
  }

  // Out of range handling
  const auto outOfRange = zyron::analysis::ChordAnalyzer::classIndexToChord(999);
  CHECK(outOfRange.isSilent());
  CHECK(outOfRange.name == "N");
}

TEST_CASE("ChordAnalyzer: CQT analysis and chord detection on harmonic signal", "[analysis][chord]") {
  const int sampleRate = 44100;
  // A Minor triad: A4 (440 Hz), C5 (523.25 Hz), E5 (659.25 Hz)
  const auto audio = generateChordAudio({440.0f, 523.25f, 659.25f}, sampleRate, 4.0);

  zyron::analysis::ChordAnalyzer analyzer;
  const auto result = analyzer.analyze(audio.data(), audio.size(), sampleRate);

  REQUIRE(result.success);
  CHECK_FALSE(result.chordProgression.empty());
  CHECK(result.dominantChord == "Am");
  CHECK(result.estimatedKey.camelot == "8A");

  // Check chroma profile has prominent peaks at A (9), C (0), E (4)
  REQUIRE(result.chromaProfile.size() == 12);
  CHECK(result.chromaProfile[9] > 0.15f);  // Root A
  CHECK(result.chromaProfile[0] > 0.10f);  // Minor 3rd C
  CHECK(result.chromaProfile[4] > 0.10f);  // 5th E
}

TEST_CASE("ChordAnalyzer: ChordMini ONNX inference callback execution", "[analysis][chord]") {
  const int sampleRate = 22050;
  const std::vector<float> audio(sampleRate * 2, 0.5f);  // 2 seconds

  bool callbackInvoked = false;
  std::size_t reportedFrames = 0;

  auto mockInferer = [&](const float* /*cqtFlat*/, std::size_t numFrames, std::size_t numBins) {
    callbackInvoked = true;
    reportedFrames = numFrames;
    CHECK(numBins == 144);

    // Return logits with sharp peak at class 22 ("Am")
    std::vector<float> logits(numFrames * 170, -10.0f);
    for (std::size_t t = 0; t < numFrames; ++t) {
      logits[t * 170 + 22] = 15.0f;  // Class 22 = Am
    }
    return logits;
  };

  zyron::analysis::ChordAnalyzer analyzer(mockInferer);
  const auto result = analyzer.analyze(audio.data(), audio.size(), sampleRate);

  CHECK(callbackInvoked);
  CHECK(reportedFrames > 0);
  REQUIRE(result.success);
  CHECK(result.dominantChord == "Am");
  CHECK(result.estimatedKey.camelot == "8A");
}

TEST_CASE("HarmonyAnalysisResult: progression compatibility scoring (P6-04)", "[analysis][chord]") {
  zyron::analysis::HarmonyAnalysisResult trackA;
  trackA.estimatedKey = zyron::analysis::MusicalKey::fromCamelot("8A");  // Am
  trackA.chromaProfile.assign(12, 0.05f);
  trackA.chromaProfile[9] = 0.40f;  // A
  trackA.chromaProfile[0] = 0.25f;  // C
  trackA.chromaProfile[4] = 0.25f;  // E

  zyron::analysis::HarmonyAnalysisResult trackB_Same;
  trackB_Same.estimatedKey = zyron::analysis::MusicalKey::fromCamelot("8A");  // Am
  trackB_Same.chromaProfile = trackA.chromaProfile;

  zyron::analysis::HarmonyAnalysisResult trackC_Relative;
  trackC_Relative.estimatedKey = zyron::analysis::MusicalKey::fromCamelot("8B");  // C Major (relative)
  trackC_Relative.chromaProfile.assign(12, 0.05f);
  trackC_Relative.chromaProfile[0] = 0.40f;  // C
  trackC_Relative.chromaProfile[4] = 0.25f;  // E
  trackC_Relative.chromaProfile[7] = 0.25f;  // G

  zyron::analysis::HarmonyAnalysisResult trackD_Clash;
  trackD_Clash.estimatedKey = zyron::analysis::MusicalKey::fromCamelot("3A");  // Bbm (dissonant/clash)
  trackD_Clash.chromaProfile.assign(12, 0.05f);
  trackD_Clash.chromaProfile[10] = 0.40f;  // Bb
  trackD_Clash.chromaProfile[1] = 0.25f;   // Db
  trackD_Clash.chromaProfile[5] = 0.25f;   // F

  const float compatSame = zyron::analysis::HarmonyAnalysisResult::progressionCompatibility(trackA, trackB_Same);
  const float compatRel = zyron::analysis::HarmonyAnalysisResult::progressionCompatibility(trackA, trackC_Relative);
  const float compatClash = zyron::analysis::HarmonyAnalysisResult::progressionCompatibility(trackA, trackD_Clash);

  CHECK(compatSame >= 0.90f);
  CHECK(compatRel >= 0.75f);
  CHECK(compatClash < 0.50f);
  CHECK(compatSame > compatRel);
  CHECK(compatRel > compatClash);
}
