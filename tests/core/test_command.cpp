// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>
#include <vector>

#include "Core/Commands/Command.hpp"

using namespace zyron::core;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

AppState stateWithTrackOnA() {
  AppState state;
  state.decks[index(DeckId::A)].track = TrackId{42};
  return state;
}

}  // namespace

TEST_CASE("commandName returns the wire names from SPEC section 50") {
  CHECK(commandName(LoadTrack{DeckId::A, TrackId{1}}) == "LOAD_TRACK");
  CHECK(commandName(UnloadTrack{DeckId::A}) == "UNLOAD_TRACK");
  CHECK(commandName(Play{DeckId::A}) == "PLAY");
  CHECK(commandName(Pause{DeckId::A}) == "PAUSE");
  CHECK(commandName(Cue{DeckId::A}) == "CUE");
  CHECK(commandName(SetGain{DeckId::A, 0.0F}) == "SET_GAIN");
  CHECK(commandName(SetVolume{DeckId::A, 1.0F}) == "SET_VOLUME");
  CHECK(commandName(SetEq{DeckId::A, EqBand::Low, 0.0F}) == "SET_EQ");
  CHECK(commandName(SetAudioOutput{}) == "SET_AUDIO_OUTPUT");
  CHECK(commandName(SetTestTone{}) == "SET_TEST_TONE");
}

TEST_CASE("device-wide commands address no deck") {
  CHECK_FALSE(targetDeck(SetAudioOutput{}).has_value());
  CHECK_FALSE(targetDeck(SetTestTone{}).has_value());
}

TEST_CASE("targetDeck reports the deck every command addresses") {
  CHECK(targetDeck(Play{DeckId::C}) == DeckId::C);
  CHECK(targetDeck(SetEq{DeckId::D, EqBand::High, 1.0F}) == DeckId::D);
  CHECK(targetDeck(LoadTrack{DeckId::B, TrackId{7}}) == DeckId::B);
}

TEST_CASE("validate rejects out-of-range, non-finite and malformed arguments") {
  struct Row {
    Command command;
    CommandErrorCode expected;
  };
  const std::vector<Row> rows{
      {SetGain{DeckId::A, kNaN}, CommandErrorCode::NotFinite},
      {SetGain{DeckId::A, kInf}, CommandErrorCode::NotFinite},
      {SetGain{DeckId::A, -kInf}, CommandErrorCode::NotFinite},
      {SetVolume{DeckId::A, -kInf}, CommandErrorCode::NotFinite},
      {SetEq{DeckId::A, EqBand::Low, -kInf}, CommandErrorCode::NotFinite},
      {SetGain{DeckId::A, limits::kGainMaxDb + 0.5F}, CommandErrorCode::OutOfRange},
      {SetGain{DeckId::A, limits::kGainMinDb - 0.5F}, CommandErrorCode::OutOfRange},
      {SetVolume{DeckId::A, -0.01F}, CommandErrorCode::OutOfRange},
      {SetVolume{DeckId::A, 1.01F}, CommandErrorCode::OutOfRange},
      {SetVolume{DeckId::A, kNaN}, CommandErrorCode::NotFinite},
      {SetEq{DeckId::A, EqBand::Low, limits::kEqMinDb - 1.0F}, CommandErrorCode::OutOfRange},
      {SetEq{DeckId::A, EqBand::Mid, limits::kEqMaxDb + 1.0F}, CommandErrorCode::OutOfRange},
      {SetEq{DeckId::A, EqBand::High, kNaN}, CommandErrorCode::NotFinite},
      {SetEq{DeckId::A, static_cast<EqBand>(7), 0.0F}, CommandErrorCode::InvalidBand},
      {LoadTrack{DeckId::A, TrackId{0}}, CommandErrorCode::InvalidTrack},
      {LoadTrack{DeckId::A, TrackId{-5}}, CommandErrorCode::InvalidTrack},
      {Play{static_cast<DeckId>(9)}, CommandErrorCode::InvalidDeck},
      {UnloadTrack{static_cast<DeckId>(4)}, CommandErrorCode::InvalidDeck},
      {LoadTrack{static_cast<DeckId>(200), TrackId{1}}, CommandErrorCode::InvalidDeck},
      {SetGain{static_cast<DeckId>(7), 0.0F}, CommandErrorCode::InvalidDeck},
      {SetEq{static_cast<DeckId>(5), EqBand::Low, 0.0F}, CommandErrorCode::InvalidDeck},
      {SetTestTone{true, kNaN, -20.0F}, CommandErrorCode::NotFinite},
      {SetTestTone{true, 440.0F, kNaN}, CommandErrorCode::NotFinite},
      {SetTestTone{true, kInf, -20.0F}, CommandErrorCode::NotFinite},
      {SetTestTone{true, limits::kToneFrequencyMinHz - 1.0F, -20.0F}, CommandErrorCode::OutOfRange},
      {SetTestTone{true, limits::kToneFrequencyMaxHz + 1.0F, -20.0F}, CommandErrorCode::OutOfRange},
      {SetTestTone{true, 440.0F, limits::kToneLevelMinDb - 1.0F}, CommandErrorCode::OutOfRange},
      {SetTestTone{true, 440.0F, limits::kToneLevelMaxDb + 1.0F}, CommandErrorCode::OutOfRange},
      {SetAudioOutput{"", "", kNaN, 256}, CommandErrorCode::NotFinite},
      {SetAudioOutput{"", "", limits::kSampleRateMin - 1.0, 256}, CommandErrorCode::OutOfRange},
      {SetAudioOutput{"", "", limits::kSampleRateMax + 1.0, 256}, CommandErrorCode::OutOfRange},
      {SetAudioOutput{"", "", 48000.0, limits::kBufferSizeMin - 1}, CommandErrorCode::OutOfRange},
      {SetAudioOutput{"", "", 48000.0, limits::kBufferSizeMax + 1}, CommandErrorCode::OutOfRange},
      {SetAudioOutput{"", "Speakers\nrm -rf", 48000.0, 256}, CommandErrorCode::InvalidName},
      {SetAudioOutput{"Win\tdows", "", 48000.0, 256}, CommandErrorCode::InvalidName},
      {SetAudioOutput{"", std::string(limits::kNameMaxLength + 1, 'x'), 48000.0, 256}, CommandErrorCode::InvalidName},
  };

  const AppState state = stateWithTrackOnA();
  for (const Row& row : rows) {
    INFO("command: " << commandName(row.command));
    const auto error = validate(row.command, state);
    REQUIRE(error.has_value());
    CHECK(error->code == row.expected);
    CHECK_FALSE(error->message.empty());
  }
}

TEST_CASE("validate accepts the documented boundaries") {
  const AppState state = stateWithTrackOnA();
  CHECK_FALSE(validate(SetGain{DeckId::A, limits::kGainMinDb}, state));
  CHECK_FALSE(validate(SetGain{DeckId::A, limits::kGainMaxDb}, state));
  CHECK_FALSE(validate(SetVolume{DeckId::A, 0.0F}, state));
  CHECK_FALSE(validate(SetVolume{DeckId::A, 1.0F}, state));
  CHECK_FALSE(validate(SetEq{DeckId::A, EqBand::Low, limits::kEqMinDb}, state));
  CHECK_FALSE(validate(SetEq{DeckId::A, EqBand::High, limits::kEqMaxDb}, state));
  CHECK_FALSE(validate(LoadTrack{DeckId::D, TrackId{1}}, state));
  CHECK_FALSE(validate(SetTestTone{false, limits::kToneFrequencyMinHz, limits::kToneLevelMinDb}, state));
  CHECK_FALSE(validate(SetTestTone{true, limits::kToneFrequencyMaxHz, limits::kToneLevelMaxDb}, state));
  CHECK_FALSE(validate(SetAudioOutput{"", "", limits::kSampleRateMin, limits::kBufferSizeMin}, state));
  CHECK_FALSE(validate(SetAudioOutput{"", "", limits::kSampleRateMax, limits::kBufferSizeMax}, state));
  CHECK_FALSE(validate(SetAudioOutput{"Windows Audio", "Динамики (Realtek)", 48000.0, 256}, state));  // UTF-8 is fine
  CHECK_FALSE(validate(SetAudioOutput{"", std::string(limits::kNameMaxLength, 'x'), 48000.0, 256}, state));
}

TEST_CASE("transport commands need a loaded track, unload does not") {
  const AppState state = stateWithTrackOnA();

  for (const Command& command : {Command{Play{DeckId::B}}, Command{Pause{DeckId::B}}, Command{Cue{DeckId::B}}}) {
    INFO("command: " << commandName(command));
    const auto error = validate(command, state);
    REQUIRE(error.has_value());
    CHECK(error->code == CommandErrorCode::NoTrackLoaded);
  }

  CHECK_FALSE(validate(Play{DeckId::A}, state));
  CHECK_FALSE(validate(Pause{DeckId::A}, state));
  CHECK_FALSE(validate(Cue{DeckId::A}, state));
  CHECK_FALSE(validate(UnloadTrack{DeckId::B}, state));
}

TEST_CASE("apply is pure: it returns a new state and leaves the input untouched") {
  const AppState before = stateWithTrackOnA();
  const AppState copy = before;

  const AppState after = apply(before, Play{DeckId::A});

  CHECK(before == copy);
  CHECK(after.revision == before.revision + 1);
  CHECK(after.deck(DeckId::A).playing);
  CHECK_FALSE(before.deck(DeckId::A).playing);
}

TEST_CASE("apply refuses a deck id that validate() would have rejected") {
  const AppState state;
  CHECK_THROWS_AS(apply(state, Play{static_cast<DeckId>(9)}), std::out_of_range);
}

TEST_CASE("apply implements deck transport semantics") {
  AppState state = stateWithTrackOnA();

  state = apply(state, Play{DeckId::A});
  CHECK(state.deck(DeckId::A).playing);

  state = apply(state, Pause{DeckId::A});
  CHECK_FALSE(state.deck(DeckId::A).playing);

  state = apply(state, Play{DeckId::A});
  state = apply(state, Cue{DeckId::A});
  CHECK_FALSE(state.deck(DeckId::A).playing);

  state = apply(state, Play{DeckId::A});
  state = apply(state, LoadTrack{DeckId::A, TrackId{99}});
  CHECK(state.deck(DeckId::A).track == TrackId{99});
  CHECK_FALSE(state.deck(DeckId::A).playing);

  state = apply(state, UnloadTrack{DeckId::A});
  CHECK_FALSE(state.deck(DeckId::A).hasTrack());
  CHECK_FALSE(state.deck(DeckId::A).playing);
}

TEST_CASE("mixer parameters only touch the addressed deck and band") {
  const AppState start;

  const AppState gained = apply(start, SetGain{DeckId::B, -6.0F});
  CHECK(gained.deck(DeckId::B).gainDb == -6.0F);
  CHECK(gained.deck(DeckId::A) == start.deck(DeckId::A));

  const AppState volumed = apply(start, SetVolume{DeckId::C, 0.25F});
  CHECK(volumed.deck(DeckId::C).volume == 0.25F);
  CHECK(volumed.deck(DeckId::D) == start.deck(DeckId::D));

  const AppState eqd = apply(start, SetEq{DeckId::A, EqBand::Mid, -12.0F});
  CHECK(eqd.deck(DeckId::A).eqDb[index(EqBand::Mid)] == -12.0F);
  CHECK(eqd.deck(DeckId::A).eqDb[index(EqBand::Low)] == 0.0F);
  CHECK(eqd.deck(DeckId::A).eqDb[index(EqBand::High)] == 0.0F);
}

TEST_CASE("audio output and test tone settings land in their own state sections") {
  const AppState start;
  CHECK(start.audioOutput.deviceName.empty());  // empty = the system default device
  CHECK_FALSE(start.testTone.enabled);

  const AppState output = apply(start, SetAudioOutput{"Windows Audio", "Speakers", 96000.0, 128});
  CHECK(output.audioOutput.apiName == "Windows Audio");
  CHECK(output.audioOutput.deviceName == "Speakers");
  CHECK(output.audioOutput.sampleRate == 96000.0);
  CHECK(output.audioOutput.bufferSize == 128);
  CHECK(output.revision == 1);
  CHECK(output.decks == start.decks);

  const AppState tone = apply(output, SetTestTone{true, 1000.0F, -12.0F});
  CHECK(tone.testTone.enabled);
  CHECK(tone.testTone.frequencyHz == 1000.0F);
  CHECK(tone.testTone.levelDb == -12.0F);
  CHECK(tone.audioOutput == output.audioOutput);
  CHECK(tone.revision == 2);
}

TEST_CASE("unloading a deck keeps its mixer knobs where the user left them") {
  AppState state = stateWithTrackOnA();
  state = apply(state, SetGain{DeckId::A, 3.0F});
  state = apply(state, SetVolume{DeckId::A, 0.5F});

  state = apply(state, UnloadTrack{DeckId::A});

  CHECK(state.deck(DeckId::A).gainDb == 3.0F);
  CHECK(state.deck(DeckId::A).volume == 0.5F);
}

TEST_CASE("Mixer commands update AppState.mixer and validate ranges") {
  AppState state;

  SECTION("commandName returns wire names") {
    CHECK(commandName(SetCrossfader{0.0F}) == "SET_CROSSFADER");
    CHECK(commandName(SetCrossfaderCurve{CrossfaderCurve::Linear}) == "SET_CROSSFADER_CURVE");
    CHECK(commandName(SetCrossfaderAssign{DeckId::C, CrossfaderAssign::Thru}) == "SET_CROSSFADER_ASSIGN");
    CHECK(commandName(SetMasterGain{0.0F}) == "SET_MASTER_GAIN");
    CHECK(commandName(SetDeckCue{DeckId::B, true}) == "SET_DECK_CUE");
  }

  SECTION("targetDeck for mixer commands") {
    CHECK_FALSE(targetDeck(SetCrossfader{0.5F}).has_value());
    CHECK_FALSE(targetDeck(SetCrossfaderCurve{CrossfaderCurve::Cut}).has_value());
    CHECK_FALSE(targetDeck(SetMasterGain{-3.0F}).has_value());
    CHECK(targetDeck(SetCrossfaderAssign{DeckId::C, CrossfaderAssign::Left}) == DeckId::C);
    CHECK(targetDeck(SetDeckCue{DeckId::D, true}) == DeckId::D);
  }

  SECTION("Validation rejects out of range") {
    CHECK(validate(SetCrossfader{-1.5F}, state).has_value());
    CHECK(validate(SetCrossfader{1.5F}, state).has_value());
    CHECK_FALSE(validate(SetCrossfader{0.0F}, state).has_value());

    CHECK(validate(SetMasterGain{-70.0F}, state).has_value());
    CHECK(validate(SetMasterGain{20.0F}, state).has_value());
    CHECK_FALSE(validate(SetMasterGain{0.0F}, state).has_value());
  }

  SECTION("apply modifies MixerState") {
    state = apply(state, SetCrossfader{0.75F});
    CHECK(state.mixer.crossfader == 0.75F);

    state = apply(state, SetCrossfaderCurve{CrossfaderCurve::Cut});
    CHECK(state.mixer.curve == CrossfaderCurve::Cut);

    state = apply(state, SetCrossfaderAssign{DeckId::C, CrossfaderAssign::Thru});
    CHECK(state.mixer.assigns.at(index(DeckId::C)) == CrossfaderAssign::Thru);

    state = apply(state, SetMasterGain{-6.0F});
    CHECK(state.mixer.masterGainDb == -6.0F);

    state = apply(state, SetDeckCue{DeckId::B, true});
    CHECK(state.mixer.cue.at(index(DeckId::B)) == true);
  }
}
