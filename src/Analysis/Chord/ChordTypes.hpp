// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Analysis/Key/MusicalKey.hpp"

namespace zyron::analysis {

/// Chord quality category (SPEC section 54, docs/AI_MODELS.md, ChordMini 170-class).
enum class ChordQuality : std::uint8_t {
  None = 0,
  Major,
  Minor,
  Dominant7,
  Major7,
  Minor7,
  Diminished,
  Augmented,
  Sus4,
  Sus2,
  Sixth,
  MinorSixth,
  Dominant7Sus4,
  Diminished7,
  HalfDiminished7
};

[[nodiscard]] constexpr std::string_view chordQualityName(ChordQuality q) noexcept {
  switch (q) {
    case ChordQuality::None: return "N";
    case ChordQuality::Major: return "maj";
    case ChordQuality::Minor: return "min";
    case ChordQuality::Dominant7: return "7";
    case ChordQuality::Major7: return "maj7";
    case ChordQuality::Minor7: return "min7";
    case ChordQuality::Diminished: return "dim";
    case ChordQuality::Augmented: return "aug";
    case ChordQuality::Sus4: return "sus4";
    case ChordQuality::Sus2: return "sus2";
    case ChordQuality::Sixth: return "6";
    case ChordQuality::MinorSixth: return "min6";
    case ChordQuality::Dominant7Sus4: return "7sus4";
    case ChordQuality::Diminished7: return "dim7";
    case ChordQuality::HalfDiminished7: return "hdim7";
  }
  return "Unknown";
}

/// Recognized chord instance.
struct Chord {
  int rootNote{-1};              // 0 = C, 1 = C#, ..., 11 = B. -1 = None / Silence
  ChordQuality quality{ChordQuality::None};
  std::string name{"N"};         // e.g. "Am", "C", "G7", "F#m", "N"
  float confidence{0.0f};        // 0.0 .. 1.0

  [[nodiscard]] bool isSilent() const noexcept {
    return rootNote < 0 || quality == ChordQuality::None || name == "N";
  }
};

/// A recognized chord spanning a time window within a track.
struct ChordSegment {
  Chord chord;
  double startSec{0.0};
  double endSec{0.0};
  std::int64_t startFrame{0};
  std::int64_t endFrame{0};
};

/// Comprehensive harmony and chord progression analysis result (SPEC section 54, ROADMAP P6-04).
struct HarmonyAnalysisResult {
  std::vector<ChordSegment> chordProgression;
  MusicalKey estimatedKey;                 // Detected overall musical key
  std::string dominantChord{"N"};          // Most frequent musical chord
  std::vector<float> chromaProfile;        // 12-dimensional pitch class energy distribution (C .. B)
  bool success{false};

  /// Computes harmonic transition compatibility score (0.0 .. 1.0) between two tracks'
  /// harmonic profiles and chord progressions (SPEC sections 52, 57).
  [[nodiscard]] static float progressionCompatibility(
      const HarmonyAnalysisResult& outgoing,
      const HarmonyAnalysisResult& incoming) noexcept;
};

}  // namespace zyron::analysis
