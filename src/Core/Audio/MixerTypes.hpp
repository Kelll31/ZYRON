// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Crossfader curve characteristics (SPEC section 20).
enum class CrossfaderCurve : std::uint8_t {
  Linear,         // Linear fade (-6 dB center)
  ConstantPower,  // Equal acoustic power (-3 dB center)
  Cut             // Sharp scratch/cut curve (steep edges, 100% in middle)
};

/// Per-channel routing assignment relative to the crossfader (SPEC section 20).
enum class CrossfaderAssign : std::uint8_t {
  Left,   // Scaled by Left crossfader curve
  Right,  // Scaled by Right crossfader curve
  Thru    // Bypasses crossfader (straight to master bus)
};

[[nodiscard]] constexpr std::string_view crossfaderCurveName(CrossfaderCurve curve) noexcept {
  switch (curve) {
    case CrossfaderCurve::Linear:
      return "Linear";
    case CrossfaderCurve::ConstantPower:
      return "ConstantPower";
    case CrossfaderCurve::Cut:
      return "Cut";
  }
  return "Unknown";
}

[[nodiscard]] constexpr std::string_view crossfaderAssignName(CrossfaderAssign assign) noexcept {
  switch (assign) {
    case CrossfaderAssign::Left:
      return "Left";
    case CrossfaderAssign::Right:
      return "Right";
    case CrossfaderAssign::Thru:
      return "Thru";
  }
  return "Unknown";
}

/// Control-thread state of the mixer (SPEC section 20).
/// Default assignments per SPEC section 20: A,C -> Left; B,D -> Right.
struct MixerState {
  float crossfader{0.0F};  // -1.0 (Left) to +1.0 (Right)
  CrossfaderCurve curve{CrossfaderCurve::ConstantPower};
  std::array<CrossfaderAssign, kDeckCount> assigns{
      CrossfaderAssign::Left,   // Deck A
      CrossfaderAssign::Right,  // Deck B
      CrossfaderAssign::Left,   // Deck C
      CrossfaderAssign::Right   // Deck D
  };
  float masterGainDb{0.0F};
  std::array<bool, kDeckCount> cue{false, false, false, false};

  friend bool operator==(const MixerState&, const MixerState&) = default;
};

}  // namespace zyron::core
