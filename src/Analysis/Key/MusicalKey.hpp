// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <string_view>

namespace zyron::analysis {

struct MusicalKey {
  std::string name;             // e.g. "Am", "C", "F#m"
  std::string camelot;          // e.g. "8A", "8B", "11A"
  int camelotNumber{8};         // 1..12
  char camelotLetter{'A'};       // 'A' (minor) or 'B' (major)
  float confidence{0.0f};

  [[nodiscard]] bool isValid() const noexcept {
    return !camelot.empty() && camelotNumber >= 1 && camelotNumber <= 12 &&
           (camelotLetter == 'A' || camelotLetter == 'B');
  }

  /// Calculates harmonic mixing compatibility score (1.0 = same, 0.9 = relative, 0.85 = adjacent).
  [[nodiscard]] static float compatibilityScore(const MusicalKey& a, const MusicalKey& b) noexcept;

  /// Returns true if two keys are compatible for seamless harmonic mixing.
  [[nodiscard]] static bool isHarmonicallyCompatible(const MusicalKey& a, const MusicalKey& b) noexcept;

  /// Parses Camelot notation (e.g. "8A", "11B") to MusicalKey.
  [[nodiscard]] static MusicalKey fromCamelot(std::string_view camelot);

  /// Converts class index (0..23, standard S-KEY order: 0..11 Major, 12..23 Minor) to MusicalKey.
  [[nodiscard]] static MusicalKey fromClassIndex(int classIndex, float confidence = 1.0f);
};

}  // namespace zyron::analysis
