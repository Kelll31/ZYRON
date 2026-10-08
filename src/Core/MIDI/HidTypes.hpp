// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/CommandTypes.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Types of physical inputs on HID controllers (SPEC section 49, ROADMAP P9-04).
enum class HidElementType : std::uint8_t {
  Button = 0,
  Axis,
  HatSwitch,
  JogWheel
};

/// A normalized event from an HID device.
struct HidEvent {
  int deviceIndex{0};
  int elementIndex{0};
  HidElementType type{HidElementType::Button};
  float value{0.0f};         // 0.0/1.0 for buttons, -1.0 .. +1.0 for bipolar axes, or relative ticks for jog wheel
  double timestampSec{0.0};
};

/// Rule binding an HID button or axis to a Command API instruction.
struct HidMappingEntry {
  int elementIndex{0};
  HidElementType type{HidElementType::Button};
  std::string targetCommandName;
  std::optional<DeckId> deck;
  std::optional<EqBand> eqBand;
  std::optional<StemKind> stem;
  float minParamValue{0.0f};
  float maxParamValue{1.0f};
  bool invert{false};
};

/// Controller profile for HID hardware devices (SPEC section 49).
struct HidProfile {
  int version{1};
  std::string profileName{"Generic Gamepad / RC Profile"};
  std::string controllerModel{"RadioMaster Boxer / Gamepad"};
  std::string description;
  std::vector<HidMappingEntry> mappings;
};

}  // namespace zyron::core
