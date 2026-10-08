// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "Core/Audio/MixerTypes.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Control-thread state of one stem inside a deck (SPEC section 43).
struct StemState {
  float volume{1.0F};  // linear volume fader, 0..1
  bool muted{false};
  bool solo{false};
  bool cue{false};     // headphone pre-fader listen for this stem

  friend bool operator==(const StemState&, const StemState&) = default;
};

/// The loop region the user asked for, in seconds from the track start.
struct LoopState {
  bool active{false};
  double startSeconds{0.0};
  double endSeconds{0.0};

  friend bool operator==(const LoopState&, const LoopState&) = default;
};

/// One effect slot of a channel strip as the user set it (SetFx). Wet and param are normalised 0..1; what `param` means
/// depends on the type (echo/delay: feedback, reverb: room size, flanger/phaser: rate).
struct FxSlotState {
  FxType type{FxType::None};
  bool enabled{false};
  float wet{0.5F};
  float param{0.5F};
  bool tailAfterFader{true};  // true: the effect sits after the fader, so echoes and reverb keep ringing when it closes

  friend bool operator==(const FxSlotState&, const FxSlotState&) = default;
};

/// User-intent state of one deck (SPEC section 14). Playback position, meters and other audio-thread facts are
/// telemetry, not state: they never live here (ARCHITECTURE section 6).
struct DeckState {
  TrackId track{};
  bool playing{false};
  float gainDb{0.0F};
  float volume{1.0F};  // linear fader position, 0..1
  double playbackSpeed{1.0};
  LoopState loop{};
  std::array<float, kEqBandCount> eqDb{};
  float filter{0.0F};  // -1 low-pass .. 0 off .. +1 high-pass
  std::array<StemState, kStemKindCount> stems{};
  bool keylock{true};           // master tempo: a tempo other than 1.0 keeps the pitch (SetKeylock)
  float keyShift{0.0F};         // semitones, -6..+6, independent of the tempo (SetKeyShift); persists across track loads
  float trackGainTrimDb{0.0F};  // automatic loudness trim of the loaded track, -12..+12 dB (SetTrackGainTrim)
  std::array<FxSlotState, kFxSlotCount> fx{};
  double fxBeatSeconds{0.0};    // tempo the beat-synced effects follow; 0 = not set (SetFxTempo)

  [[nodiscard]] constexpr bool hasTrack() const noexcept { return track.isValid(); }
  friend bool operator==(const DeckState&, const DeckState&) = default;
};

/// The audio output the user asked for. This is a request, not a fact: the engine reports what it actually obtained
/// (device, rate, buffer, errors) through AudioEngineStats. Empty names mean "the system default".
struct AudioOutputSettings {
  std::string apiName;     // audio API / driver type, e.g. "Windows Audio"; empty = keep the current one
  std::string deviceName;  // empty = the API's default output device
  double sampleRate{48000.0};
  int bufferSize{256};  // frames

  friend bool operator==(const AudioOutputSettings&, const AudioOutputSettings&) = default;
};

/// The output-path check tone (ROADMAP P1-05). Off by default; never starts by itself.
struct TestToneState {
  bool enabled{false};
  float frequencyHz{440.0F};
  float levelDb{-20.0F};

  friend bool operator==(const TestToneState&, const TestToneState&) = default;
};

/// The master bus processing in front of the output (SetMasterProcessing).
struct MasterProcessingState {
  bool glue{true};     // gentle bus compressor before the limiter
  bool limiter{true};  // brickwall limiter at -0.3 dBFS

  friend bool operator==(const MasterProcessingState&, const MasterProcessingState&) = default;
};

/// Immutable-by-convention application state: every change produces a new value with a higher `revision`.
struct AppState {
  std::array<DeckState, kDeckCount> decks{};
  MixerState mixer{};
  AudioOutputSettings audioOutput{};
  TestToneState testTone{};
  MasterProcessingState masterProcessing{};
  bool recording{false};  // the master output is being written to a file
  std::uint64_t revision{0};

  /// Throws std::out_of_range for an id that is not one of the four decks.
  [[nodiscard]] const DeckState& deck(DeckId id) const { return decks.at(index(id)); }
  friend bool operator==(const AppState&, const AppState&) = default;
};

/// Holds the current AppState and hands out immutable snapshots.
///
/// Thread-safety: safe from any non-realtime thread. It takes a mutex, so it must NEVER be called from the audio
/// thread; the engine receives its own copy of what it needs through the command bridge (ARCHITECTURE section 6).
class StateStore {
 public:
  StateStore();

  [[nodiscard]] std::shared_ptr<const AppState> snapshot() const;

  /// Compare-and-swap on the revision: replaces the current state with `next` only if the current revision is still
  /// `expectedRevision`. Returns false (and changes nothing) when another writer got there first. This keeps the
  /// "one logical writer" rule honest: a stray second writer cannot silently overwrite or rewind the state.
  [[nodiscard]] bool publishIfRevision(std::uint64_t expectedRevision, AppState next);

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const AppState> current_;
};

}  // namespace zyron::core
