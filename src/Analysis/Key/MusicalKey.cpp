// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Key/MusicalKey.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace zyron::analysis {

namespace {

struct KeyInfo {
  const char* name;
  const char* camelot;
  int number;
  char letter;
};

// S-KEY 24-class ordering: 0..11 Major (C, Db, D, Eb, E, F, F#, G, Ab, A, Bb, B),
//                         12..23 Minor (Cm, Dbm, Dm, Ebm, Em, Fm, F#m, Gm, Abm, Am, Bbm, Bm)
constexpr std::array<KeyInfo, 24> kKeyCatalog = {{
    // Major (0..11)
    {"C", "8B", 8, 'B'},
    {"Db", "3B", 3, 'B'},
    {"D", "10B", 10, 'B'},
    {"Eb", "5B", 5, 'B'},
    {"E", "12B", 12, 'B'},
    {"F", "7B", 7, 'B'},
    {"F#", "2B", 2, 'B'},
    {"G", "9B", 9, 'B'},
    {"Ab", "4B", 4, 'B'},
    {"A", "11B", 11, 'B'},
    {"Bb", "6B", 6, 'B'},
    {"B", "1B", 1, 'B'},

    // Minor (12..23)
    {"Cm", "5A", 5, 'A'},
    {"C#m", "12A", 12, 'A'},
    {"Dm", "7A", 7, 'A'},
    {"Ebm", "2A", 2, 'A'},
    {"Em", "9A", 9, 'A'},
    {"Fm", "4A", 4, 'A'},
    {"F#m", "11A", 11, 'A'},
    {"Gm", "6A", 6, 'A'},
    {"Abm", "1A", 1, 'A'},
    {"Am", "8A", 8, 'A'},
    {"Bbm", "3A", 3, 'A'},
    {"Bm", "10A", 10, 'A'},
}};

}  // namespace

float MusicalKey::compatibilityScore(const MusicalKey& a, const MusicalKey& b) noexcept {
  if (!a.isValid() || !b.isValid()) return 0.0f;

  // Exact match
  if (a.camelotNumber == b.camelotNumber && a.camelotLetter == b.camelotLetter) {
    return 1.0f;
  }

  // Relative major/minor (e.g. 8A <-> 8B, A minor <-> C major)
  if (a.camelotNumber == b.camelotNumber && a.camelotLetter != b.camelotLetter) {
    return 0.9f;
  }

  // Circular distance on Camelot clock (1..12)
  const int diff = std::abs(a.camelotNumber - b.camelotNumber);
  const int circularDist = std::min(diff, 12 - diff);

  // Adjacent on the circle of fifths (e.g. 8A <-> 7A or 9A)
  if (circularDist == 1 && a.camelotLetter == b.camelotLetter) {
    return 0.85f;
  }

  // Diagonal transition (e.g. 8A <-> 7B or 9B)
  if (circularDist == 1 && a.camelotLetter != b.camelotLetter) {
    return 0.70f;
  }

  // Energy boost / drop (+2 on wheel, e.g. 8A -> 10A)
  if (circularDist == 2 && a.camelotLetter == b.camelotLetter) {
    return 0.60f;
  }

  return 0.0f;
}

bool MusicalKey::isHarmonicallyCompatible(const MusicalKey& a, const MusicalKey& b) noexcept {
  return compatibilityScore(a, b) >= 0.70f;
}

MusicalKey MusicalKey::fromCamelot(std::string_view camelot) {
  if (camelot.size() < 2) return {};

  int number = 0;
  std::size_t letterIdx = 0;
  for (std::size_t i = 0; i < camelot.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(camelot[i]))) {
      number = number * 10 + (camelot[i] - '0');
    } else {
      letterIdx = i;
      break;
    }
  }

  if (number < 1 || number > 12 || letterIdx >= camelot.size()) {
    return {};
  }

  const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(camelot[letterIdx])));
  if (letter != 'A' && letter != 'B') {
    return {};
  }

  // Lookup key name in catalog
  for (const auto& item : kKeyCatalog) {
    if (item.number == number && item.letter == letter) {
      MusicalKey k;
      k.name = item.name;
      k.camelot = item.camelot;
      k.camelotNumber = item.number;
      k.camelotLetter = item.letter;
      k.confidence = 1.0f;
      return k;
    }
  }

  return {};
}

MusicalKey MusicalKey::fromClassIndex(int classIndex, float confidence) {
  if (classIndex < 0 || classIndex >= static_cast<int>(kKeyCatalog.size())) {
    return {};
  }
  const auto& item = kKeyCatalog[static_cast<std::size_t>(classIndex)];
  MusicalKey k;
  k.name = item.name;
  k.camelot = item.camelot;
  k.camelotNumber = item.number;
  k.camelotLetter = item.letter;
  k.confidence = confidence;
  return k;
}

}  // namespace zyron::analysis
