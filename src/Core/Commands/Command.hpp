// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "Core/Commands/CommandTypes.hpp"
#include "Core/State/AppState.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Accepted parameter ranges. Provisional until the DSP exists (ROADMAP Phase 2) - keep them in this one place.
namespace limits {
inline constexpr float kGainMinDb = -24.0F;
inline constexpr float kGainMaxDb = 12.0F;
inline constexpr float kEqMinDb = -60.0F;  // the DSP treats the floor as a full "kill"
inline constexpr float kEqMaxDb = 12.0F;
inline constexpr float kVolumeMin = 0.0F;
inline constexpr float kVolumeMax = 1.0F;

inline constexpr double kSampleRateMin = 8000.0;
inline constexpr double kSampleRateMax = 384000.0;
inline constexpr int kBufferSizeMin = 16;
inline constexpr int kBufferSizeMax = 8192;
inline constexpr std::size_t kNameMaxLength = 256;  // device / audio API names

inline constexpr float kToneFrequencyMinHz = 20.0F;
inline constexpr float kToneFrequencyMaxHz = 20000.0F;
inline constexpr float kToneLevelMinDb = -96.0F;
inline constexpr float kToneLevelMaxDb = 0.0F;
}  // namespace limits

// Value types only: ids and numbers, never pointers into the engine (ARCHITECTURE section 5).
struct LoadTrack {
  DeckId deck;
  TrackId track;
};
struct UnloadTrack {
  DeckId deck;
};
struct Play {
  DeckId deck;
};
struct Pause {
  DeckId deck;
};
struct Cue {
  DeckId deck;
};
struct SetGain {
  DeckId deck;
  float db;
};
struct SetVolume {
  DeckId deck;
  float linear;
};
struct SetEq {
  DeckId deck;
  EqBand band;
  float db;
};

// Device-wide commands: they address no deck. Names are what the OS/JUCE report; an empty name means "default".
struct SetAudioOutput {
  std::string apiName;
  std::string deviceName;
  double sampleRate{48000.0};
  int bufferSize{256};
};
struct SetTestTone {
  bool enabled{false};
  float frequencyHz{440.0F};
  float levelDb{-20.0F};
};

// Stem commands (SPEC sections 43, 50)
struct SetStemVolume {
  DeckId deck;
  StemKind stem;
  float linear;
};
struct SetStemMute {
  DeckId deck;
  StemKind stem;
  bool muted;
};
struct SetStemSolo {
  DeckId deck;
  StemKind stem;
  bool solo;
};
struct SetStemCue {
  DeckId deck;
  StemKind stem;
  bool cue;
};

// Mixer & Master routing commands (SPEC section 20)
struct SetCrossfader {
  float position{0.0F};  // -1.0 (Left) to +1.0 (Right)
};
struct SetCrossfaderCurve {
  CrossfaderCurve curve{CrossfaderCurve::ConstantPower};
};
struct SetCrossfaderAssign {
  DeckId deck{DeckId::A};
  CrossfaderAssign assign{CrossfaderAssign::Left};
};
struct SetMasterGain {
  float db{0.0F};
};
struct SetDeckCue {
  DeckId deck{DeckId::A};
  bool enabled{false};
};

/// The single vocabulary through which UI, MIDI and AI act on the application (SPEC sections 8, 50, 51).
using Command =
    std::variant<LoadTrack, UnloadTrack, Play, Pause, Cue, SetGain, SetVolume, SetEq, SetAudioOutput, SetTestTone,
                 SetStemVolume, SetStemMute, SetStemSolo, SetStemCue,
                 SetCrossfader, SetCrossfaderCurve, SetCrossfaderAssign, SetMasterGain, SetDeckCue>;

/// Stable wire name (SPEC section 50), used by MIDI mapping files, AI tool schemas and logs.
[[nodiscard]] std::string_view commandName(const Command& command) noexcept;

/// The deck a command addresses; nullopt for device-wide commands (audio output, test tone).
[[nodiscard]] std::optional<DeckId> targetDeck(const Command& command) noexcept;

/// Checks a command against the current state. nullopt means "acceptable".
[[nodiscard]] std::optional<CommandError> validate(const Command& command, const AppState& state);

/// Pure state transition: returns the next state (revision + 1) and never mutates `state`.
/// Precondition: validate(command, state) returned nullopt (an invalid deck id throws std::out_of_range).
[[nodiscard]] AppState apply(const AppState& state, const Command& command);

}  // namespace zyron::core
