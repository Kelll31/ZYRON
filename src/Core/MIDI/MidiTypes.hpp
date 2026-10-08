// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandTypes.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// MIDI message event types for DJ controller hardware (SPEC section 47, ROADMAP P9-01).
enum class MidiMessageType : std::uint8_t {
  NoteOn = 0,
  NoteOff,
  ControlChange,
  PitchBend
};

/// Plain POD representation of an incoming or outgoing MIDI event (JUCE-free).
struct MidiEvent {
  MidiMessageType type{MidiMessageType::ControlChange};
  int channel{1};          // 1..16
  int number{0};           // Note number (0..127) or CC number (0..127)
  int value{0};            // 0..127 (or 0..16383 for 14-bit pitch bend)
  double timestampSec{0.0};

  [[nodiscard]] float normalizedValue() const noexcept {
    if (type == MidiMessageType::PitchBend) {
      return static_cast<float>(value - 8192) / 8192.0f;  // -1.0 .. +1.0
    }
    return static_cast<float>(value) / 127.0f;            // 0.0 .. 1.0
  }
};

/// Mapping mode for continuous controls (absolute fader/knob vs relative endless encoder).
enum class MidiMappingMode : std::uint8_t {
  Absolute = 0,            // Standard 0..127 potentiometer or fader
  RelativeTwosComplement,  // Endless encoder (+1 = 1, -1 = 127)
  RelativeBinaryOffset,    // Endless encoder (+1 = 65, -1 = 63)
  Toggle                   // Button press flips state
};

/// A single mapping rule binding a MIDI event to an application command (SPEC section 47).
struct MidiMappingEntry {
  std::string id;
  MidiMessageType messageType{MidiMessageType::ControlChange};
  int channel{1};
  int number{0};
  MidiMappingMode mode{MidiMappingMode::Absolute};

  std::string targetCommandName;    // "PLAY", "PAUSE", "CUE", "SET_VOLUME", "SET_GAIN", "SET_EQ", "SET_CROSSFADER", "SET_STEM_MUTE"
  std::optional<DeckId> deck;
  std::optional<EqBand> eqBand;
  std::optional<StemKind> stem;

  float minParamValue{0.0f};
  float maxParamValue{1.0f};
  bool invert{false};
};

/// A complete controller mapping profile (SPEC section 47, ROADMAP P9-02).
struct MidiProfile {
  int version{1};
  std::string controllerName{"Generic MIDI Controller"};
  std::string author{"ZYRON"};
  std::string description;
  std::vector<MidiMappingEntry> mappings;
};

/// Target parameter identity for interactive MIDI Learn (SPEC section 48, ROADMAP P9-03).
struct MidiLearnTarget {
  std::string commandName;
  std::optional<DeckId> deck;
  std::optional<EqBand> eqBand;
  std::optional<StemKind> stem;
  float minValue{0.0f};
  float maxValue{1.0f};
};

}  // namespace zyron::core
