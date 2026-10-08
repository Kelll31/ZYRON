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

inline constexpr double kSeekMaxSeconds = 24.0 * 3600.0;  // a track longer than a day is a bug, not a mix
inline constexpr double kSpeedMin = 0.5;
inline constexpr double kSpeedMax = 2.0;

inline constexpr float kKeyShiftMinSemitones = -6.0F;
inline constexpr float kKeyShiftMaxSemitones = 6.0F;
inline constexpr float kTrimMinDb = -12.0F;
inline constexpr float kTrimMaxDb = 12.0F;
inline constexpr double kBeatSecondsMin = 0.15;  // 400 BPM
inline constexpr double kBeatSecondsMax = 3.0;   // 20 BPM
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
/// Plays a scratch pattern for `beats` beats of `beatSeconds` each, then carries on where the record would have been
/// without it (a backspin stops the deck instead).
struct Scratch {
  DeckId deck;
  ScratchPattern pattern;
  double beats;
  double beatSeconds;
};
/// Moves the deck's speed to `speed` gradually over `seconds` (a tempo that settles without anyone noticing).
struct GlideTempo {
  DeckId deck;
  double speed;
  double seconds;
};
/// The channel's DJ filter: -1 full low-pass, 0 off, +1 full high-pass (SPEC section 20).
struct SetFilter {
  DeckId deck;
  float position;
};
/// Keylock (master tempo, default on): a tempo other than 1.0 keeps the pitch. Off: varispeed like a turntable. Scratches,
/// brakes and loop rolls are always varispeed.
struct SetKeylock {
  DeckId deck;
  bool enabled{true};
};
/// Pitch shift in semitones (-6..+6, fractions allowed), independent of the tempo. 0 = off.
struct SetKeyShift {
  DeckId deck;
  float semitones{0.0F};
};
/// Loads effect `type` into FX `slot` (0 or 1) of the deck's channel and sets it. `wet` and `param` are 0..1: wet is the
/// amount of effect in the signal; param is the type's main knob (echo/delay: feedback, reverb: room size, flanger and
/// phaser: rate). Echo and delay time follow SetFxTempo. `tailAfterFader` puts the effect after the channel fader, so
/// that closing the fader leaves the echoes and the reverb ringing out ("echo out"); false puts it before the fader.
/// FxType::None empties the slot. Changing the type fades the old effect out; nothing allocates.
struct SetFx {
  DeckId deck;
  int slot{0};
  FxType type{FxType::None};
  bool enabled{true};
  float wet{0.5F};
  float param{0.5F};
  bool tailAfterFader{true};
};
/// Length of one beat in seconds (60 / BPM) for the beat-synced effects of the deck's channel (echo, delay).
struct SetFxTempo {
  DeckId deck;
  double beatSeconds{0.5};
};
/// Loudness trim of the loaded track in dB (-12..+12), applied before the channel gain and smoothed. Automix sends the
/// value that brings the track to the target LUFS; it is independent of the user's SetGain knob.
struct SetTrackGainTrim {
  DeckId deck;
  float db{0.0F};
};
/// Master bus: the glue compressor (about 2:1, slow attack, no makeup) and the -0.3 dBFS brickwall limiter. Both on by
/// default; the limiter should stay on unless something downstream limits.
struct SetMasterProcessing {
  bool glue{true};
  bool limiter{true};
};

/// A synthesized DJ performance hit (air horn, siren, riser, downlifter, impact, laser) mixed into the master bus in
/// front of the glue compressor and the limiter. `level` 0..1 (1 is about -6 dBFS peak). `beatSeconds` 0 = free
/// (a 0.5 s beat is assumed); otherwise 60 / BPM in [kBeatSecondsMin, kBeatSecondsMax]: the hit's length and the air
/// horn's stabs follow the beat. A gesture: the state does not change.
struct TriggerFxHit {
  FxHitType type{FxHitType::AirHorn};
  float level{0.5F};
  double beatSeconds{0.0};
};

/// Moves the playhead. The position itself is engine telemetry; this is only the request.
struct Seek {
  DeckId deck;
  double seconds;
};
/// Varispeed ratio: 1.0 = original tempo.
struct SetPlaybackSpeed {
  DeckId deck;
  double speed;
};
/// Sets the loop region (seconds from the track start) and switches it on or off. Inactive keeps the region.
struct SetLoop {
  DeckId deck;
  double startSeconds;
  double endSeconds;
  bool active;
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

/// One-shot beat sync: matches the deck's tempo to the master deck's and aligns the beats. Needs beat grids on both
/// decks; the engine reports why when it cannot sync.
struct Sync {
  DeckId deck;
};

/// Separates the loaded track into vocals, drums, bass and other with the neural network. Runs in the background; the
/// deck switches to the stems when they are ready (and the next time the same track is loaded, at once).
struct SeparateStems {
  DeckId deck;
};

/// Starts or stops recording the master output to a file.
struct SetRecording {
  bool enabled{false};
};

/// The single vocabulary through which UI, MIDI and AI act on the application (SPEC sections 8, 50, 51).
using Command =
    std::variant<LoadTrack, UnloadTrack, Play, Pause, Cue, SetGain, SetVolume, SetEq, SetFilter, Scratch, GlideTempo, SetAudioOutput, SetTestTone,
                 SetStemVolume, SetStemMute, SetStemSolo, SetStemCue,
                 SetCrossfader, SetCrossfaderCurve, SetCrossfaderAssign, SetMasterGain, SetDeckCue, Seek, SetPlaybackSpeed,
                 SetLoop, SetRecording, Sync, SeparateStems, SetKeylock, SetKeyShift, SetFx, SetFxTempo, SetTrackGainTrim,
                 SetMasterProcessing, TriggerFxHit>;

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
