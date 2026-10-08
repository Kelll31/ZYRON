// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/Commands/Command.hpp"

#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace zyron::core {
namespace {

template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

template <class T>
std::string formatNumber(T value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());  // messages must not depend on the user's decimal separator
  out << value;
  return out.str();
}

std::optional<CommandError> makeError(CommandErrorCode code, std::string message) {
  return CommandError{code, std::move(message)};
}

template <class T>
std::optional<CommandError> checkRange(T value, T min, T max, const char* what) {
  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(value)) {
      return makeError(CommandErrorCode::NotFinite, std::string(what) + " must be a finite number");
    }
  }
  if (value < min || value > max) {
    return makeError(CommandErrorCode::OutOfRange,
                     std::string(what) + " must be within [" + formatNumber(min) + ", " + formatNumber(max) + "]");
  }
  return std::nullopt;
}

std::optional<CommandError> requireTrack(const DeckState& deck, const char* command) {
  if (!deck.hasTrack()) {
    return makeError(CommandErrorCode::NoTrackLoaded, std::string(command) + " needs a track loaded on the deck");
  }
  return std::nullopt;
}

/// Names come from the OS and from the user: bound their length and keep control characters (log injection, terminal
/// escapes, embedded newlines) out. Bytes >= 0x80 are accepted so UTF-8 device names work.
std::optional<CommandError> checkName(const std::string& name, const char* what) {
  if (name.size() > limits::kNameMaxLength) {
    return makeError(CommandErrorCode::InvalidName, std::string(what) + " is too long");
  }
  for (const char c : name) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte == 0x7f) {
      return makeError(CommandErrorCode::InvalidName, std::string(what) + " contains a control character");
    }
  }
  return std::nullopt;
}

}  // namespace

std::string_view commandName(const Command& command) noexcept {
  return std::visit(Overloaded{
                        [](const LoadTrack&) { return std::string_view{"LOAD_TRACK"}; },
                        [](const UnloadTrack&) { return std::string_view{"UNLOAD_TRACK"}; },
                        [](const Play&) { return std::string_view{"PLAY"}; },
                        [](const Pause&) { return std::string_view{"PAUSE"}; },
                        [](const Cue&) { return std::string_view{"CUE"}; },
                        [](const SetGain&) { return std::string_view{"SET_GAIN"}; },
                        [](const SetVolume&) { return std::string_view{"SET_VOLUME"}; },
                        [](const SetEq&) { return std::string_view{"SET_EQ"}; },
                        [](const SetAudioOutput&) { return std::string_view{"SET_AUDIO_OUTPUT"}; },
                        [](const SetTestTone&) { return std::string_view{"SET_TEST_TONE"}; },
                        [](const SetStemVolume&) { return std::string_view{"SET_STEM_VOLUME"}; },
                        [](const SetStemMute&) { return std::string_view{"MUTE_STEM"}; },
                        [](const SetStemSolo&) { return std::string_view{"SOLO_STEM"}; },
                        [](const SetStemCue&) { return std::string_view{"CUE_STEM"}; },
                        [](const SetCrossfader&) { return std::string_view{"SET_CROSSFADER"}; },
                        [](const SetCrossfaderCurve&) { return std::string_view{"SET_CROSSFADER_CURVE"}; },
                        [](const SetCrossfaderAssign&) { return std::string_view{"SET_CROSSFADER_ASSIGN"}; },
                        [](const SetMasterGain&) { return std::string_view{"SET_MASTER_GAIN"}; },
                        [](const SetDeckCue&) { return std::string_view{"SET_DECK_CUE"}; },
                    },
                    command);
}

std::optional<DeckId> targetDeck(const Command& command) noexcept {
  return std::visit(Overloaded{
                        [](const SetAudioOutput&) -> std::optional<DeckId> { return std::nullopt; },
                        [](const SetTestTone&) -> std::optional<DeckId> { return std::nullopt; },
                        [](const SetCrossfader&) -> std::optional<DeckId> { return std::nullopt; },
                        [](const SetCrossfaderCurve&) -> std::optional<DeckId> { return std::nullopt; },
                        [](const SetMasterGain&) -> std::optional<DeckId> { return std::nullopt; },
                        [](const auto& c) noexcept -> std::optional<DeckId> { return c.deck; },
                    },
                    command);
}

std::optional<CommandError> validate(const Command& command, const AppState& state) {
  const std::optional<DeckId> deckId = targetDeck(command);
  if (deckId && !isValid(*deckId)) {
    return makeError(CommandErrorCode::InvalidDeck, "deck id is not one of the four decks");
  }
  const DeckState* deck = deckId ? &state.deck(*deckId) : nullptr;  // null only for device-wide commands

  return std::visit(
      Overloaded{
          [](const LoadTrack& c) -> std::optional<CommandError> {
            if (!c.track.isValid()) {
              return makeError(CommandErrorCode::InvalidTrack, "track id must be positive");
            }
            return std::nullopt;
          },
          [](const UnloadTrack&) -> std::optional<CommandError> { return std::nullopt; },
          [&](const Play&) { return requireTrack(*deck, "PLAY"); },
          [&](const Pause&) { return requireTrack(*deck, "PAUSE"); },
          [&](const Cue&) { return requireTrack(*deck, "CUE"); },
          [](const SetGain& c) { return checkRange(c.db, limits::kGainMinDb, limits::kGainMaxDb, "gain"); },
          [](const SetVolume& c) { return checkRange(c.linear, limits::kVolumeMin, limits::kVolumeMax, "volume"); },
          [](const SetEq& c) -> std::optional<CommandError> {
            if (!isValid(c.band)) {
              return makeError(CommandErrorCode::InvalidBand, "EQ band is not low, mid or high");
            }
            return checkRange(c.db, limits::kEqMinDb, limits::kEqMaxDb, "EQ gain");
          },
          [](const SetAudioOutput& c) -> std::optional<CommandError> {
            if (auto error = checkName(c.apiName, "audio API name")) {
              return error;
            }
            if (auto error = checkName(c.deviceName, "audio device name")) {
              return error;
            }
            if (auto error = checkRange(c.sampleRate, limits::kSampleRateMin, limits::kSampleRateMax, "sample rate")) {
              return error;
            }
            return checkRange(c.bufferSize, limits::kBufferSizeMin, limits::kBufferSizeMax, "buffer size");
          },
          [](const SetTestTone& c) -> std::optional<CommandError> {
            if (auto error = checkRange(c.frequencyHz, limits::kToneFrequencyMinHz, limits::kToneFrequencyMaxHz,
                                        "tone frequency")) {
              return error;
            }
            return checkRange(c.levelDb, limits::kToneLevelMinDb, limits::kToneLevelMaxDb, "tone level");
          },
          [](const SetStemVolume& c) -> std::optional<CommandError> {
            if (!isValid(c.stem)) {
              return makeError(CommandErrorCode::InvalidStem, "stem is not vocals, drums, bass or other");
            }
            return checkRange(c.linear, limits::kVolumeMin, limits::kVolumeMax, "stem volume");
          },
          [](const SetStemMute& c) -> std::optional<CommandError> {
            if (!isValid(c.stem)) {
              return makeError(CommandErrorCode::InvalidStem, "stem is not vocals, drums, bass or other");
            }
            return std::nullopt;
          },
          [](const SetStemSolo& c) -> std::optional<CommandError> {
            if (!isValid(c.stem)) {
              return makeError(CommandErrorCode::InvalidStem, "stem is not vocals, drums, bass or other");
            }
            return std::nullopt;
          },
          [](const SetStemCue& c) -> std::optional<CommandError> {
            if (!isValid(c.stem)) {
              return makeError(CommandErrorCode::InvalidStem, "stem is not vocals, drums, bass or other");
            }
            return std::nullopt;
          },
          [](const SetCrossfader& c) -> std::optional<CommandError> {
            return checkRange(c.position, -1.0F, 1.0F, "crossfader position");
          },
          [](const SetCrossfaderCurve&) -> std::optional<CommandError> { return std::nullopt; },
          [](const SetCrossfaderAssign&) -> std::optional<CommandError> { return std::nullopt; },
          [](const SetMasterGain& c) -> std::optional<CommandError> {
            return checkRange(c.db, -60.0F, 12.0F, "master gain");
          },
          [](const SetDeckCue&) -> std::optional<CommandError> { return std::nullopt; },
      },
      command);
}

AppState apply(const AppState& state, const Command& command) {
  AppState next = state;
  ++next.revision;
  const std::optional<DeckId> deckId = targetDeck(command);
  DeckState* deck = deckId ? &next.decks.at(index(*deckId)) : nullptr;  // null only for device-wide commands

  std::visit(Overloaded{
                 [&](const LoadTrack& c) {
                   deck->track = c.track;
                   deck->playing = false;
                 },
                 [&](const UnloadTrack&) {
                   deck->track = TrackId{};
                   deck->playing = false;
                 },
                 [&](const Play&) { deck->playing = true; },
                 [&](const Pause&) { deck->playing = false; },
                 [&](const Cue&) { deck->playing = false; },  // CDJ-style: jump to the cue point and stop
                 [&](const SetGain& c) { deck->gainDb = c.db; },
                 [&](const SetVolume& c) { deck->volume = c.linear; },
                 [&](const SetEq& c) { deck->eqDb.at(index(c.band)) = c.db; },
                 [&](const SetAudioOutput& c) {
                   next.audioOutput = AudioOutputSettings{c.apiName, c.deviceName, c.sampleRate, c.bufferSize};
                 },
                 [&](const SetTestTone& c) { next.testTone = TestToneState{c.enabled, c.frequencyHz, c.levelDb}; },
                 [&](const SetStemVolume& c) { deck->stems.at(index(c.stem)).volume = c.linear; },
                 [&](const SetStemMute& c) { deck->stems.at(index(c.stem)).muted = c.muted; },
                 [&](const SetStemSolo& c) { deck->stems.at(index(c.stem)).solo = c.solo; },
                 [&](const SetStemCue& c) { deck->stems.at(index(c.stem)).cue = c.cue; },
                 [&](const SetCrossfader& c) { next.mixer.crossfader = c.position; },
                 [&](const SetCrossfaderCurve& c) { next.mixer.curve = c.curve; },
                 [&](const SetCrossfaderAssign& c) { next.mixer.assigns.at(index(c.deck)) = c.assign; },
                 [&](const SetMasterGain& c) { next.mixer.masterGainDb = c.db; },
                 [&](const SetDeckCue& c) { next.mixer.cue.at(index(c.deck)) = c.enabled; },
             },
             command);
  return next;
}

}  // namespace zyron::core
