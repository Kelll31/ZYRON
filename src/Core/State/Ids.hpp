// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace zyron::core {

/// The four decks of SPEC section 13. Two-deck mode simply uses A and B.
enum class DeckId : std::uint8_t { A = 0, B, C, D };
inline constexpr std::size_t kDeckCount = 4;

[[nodiscard]] constexpr std::size_t index(DeckId deck) noexcept {
  return static_cast<std::size_t>(deck);
}
/// False for values that did not come from the enumerators (e.g. a malformed MIDI mapping).
[[nodiscard]] constexpr bool isValid(DeckId deck) noexcept {
  return index(deck) < kDeckCount;
}

/// Identity of a library track. Zero (the default) means "no track"; the library assigns positive ids.
struct TrackId {
  std::int64_t value{0};

  [[nodiscard]] constexpr bool isValid() const noexcept { return value > 0; }
  friend constexpr auto operator<=>(const TrackId&, const TrackId&) = default;
};

/// The three EQ bands of SPEC section 21.
enum class EqBand : std::uint8_t { Low = 0, Mid, High };
inline constexpr std::size_t kEqBandCount = 3;

[[nodiscard]] constexpr std::size_t index(EqBand band) noexcept {
  return static_cast<std::size_t>(band);
}
[[nodiscard]] constexpr bool isValid(EqBand band) noexcept {
  return index(band) < kEqBandCount;
}

// The counts above are used to size arrays; keep them tied to the enumerators.
static_assert(index(DeckId::D) + 1 == kDeckCount, "kDeckCount must match the DeckId enumerators");
static_assert(index(EqBand::High) + 1 == kEqBandCount, "kEqBandCount must match the EqBand enumerators");

/// The four stems of SPEC section 32 (the optional 6-stem model adds more later).
enum class StemKind : std::uint8_t { Vocals = 0, Drums, Bass, Other };
inline constexpr std::size_t kStemKindCount = 4;

[[nodiscard]] constexpr std::size_t index(StemKind stem) noexcept {
  return static_cast<std::size_t>(stem);
}
[[nodiscard]] constexpr bool isValid(StemKind stem) noexcept {
  return index(stem) < kStemKindCount;
}
static_assert(index(StemKind::Other) + 1 == kStemKindCount, "kStemKindCount must match the StemKind enumerators");

[[nodiscard]] constexpr std::string_view stemKindName(StemKind stem) noexcept {
  switch (stem) {
    case StemKind::Vocals:
      return "Vocals";
    case StemKind::Drums:
      return "Drums";
    case StemKind::Bass:
      return "Bass";
    case StemKind::Other:
      return "Other";
  }
  return "Unknown";
}

}  // namespace zyron::core
