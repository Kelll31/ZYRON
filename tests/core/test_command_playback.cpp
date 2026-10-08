// SPDX-License-Identifier: AGPL-3.0-only
// Seek, SetPlaybackSpeed, SetLoop, SetRecording, Sync and SeparateStems: names, targets, validation and state transitions.
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "Core/Commands/Command.hpp"

using namespace zyron::core;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

AppState stateWithTrackOnA() {
  AppState state;
  state.decks[index(DeckId::A)].track = TrackId{42};
  return state;
}

}  // namespace

TEST_CASE("playback commands report their SPEC section 50 wire names") {
  CHECK(commandName(Seek{DeckId::A, 1.0}) == "SEEK");
  CHECK(commandName(SetPlaybackSpeed{DeckId::A, 1.0}) == "SET_PLAYBACK_SPEED");
  CHECK(commandName(SetLoop{DeckId::A, 0.0, 1.0, true}) == "SET_LOOP");
  CHECK(commandName(SetRecording{true}) == "SET_RECORDING");
  CHECK(commandName(Sync{DeckId::A}) == "SYNC");
  CHECK(commandName(SeparateStems{DeckId::A}) == "SEPARATE_STEMS");
}

TEST_CASE("targetDeck of playback commands") {
  CHECK(targetDeck(Seek{DeckId::B, 1.0}) == DeckId::B);
  CHECK(targetDeck(SetPlaybackSpeed{DeckId::C, 1.0}) == DeckId::C);
  CHECK(targetDeck(SetLoop{DeckId::D, 0.0, 1.0, true}) == DeckId::D);
  CHECK(targetDeck(Sync{DeckId::B}) == DeckId::B);
  CHECK(targetDeck(SeparateStems{DeckId::C}) == DeckId::C);
  CHECK_FALSE(targetDeck(SetRecording{true}).has_value());
}

TEST_CASE("Seek validation requires a track and a finite in-range position") {
  const AppState loaded = stateWithTrackOnA();
  CHECK_FALSE(validate(Seek{DeckId::A, 0.0}, loaded).has_value());
  CHECK_FALSE(validate(Seek{DeckId::A, limits::kSeekMaxSeconds}, loaded).has_value());
  CHECK(validate(Seek{DeckId::A, -0.001}, loaded)->code == CommandErrorCode::OutOfRange);
  CHECK(validate(Seek{DeckId::A, limits::kSeekMaxSeconds + 1.0}, loaded)->code == CommandErrorCode::OutOfRange);
  CHECK(validate(Seek{DeckId::A, kNaN}, loaded)->code == CommandErrorCode::NotFinite);
  CHECK(validate(Seek{DeckId::A, kInf}, loaded)->code == CommandErrorCode::NotFinite);
  CHECK(validate(Seek{DeckId::B, 1.0}, loaded)->code == CommandErrorCode::NoTrackLoaded);
}

TEST_CASE("SetPlaybackSpeed validation enforces the varispeed range and finiteness") {
  const AppState empty;  // no track needed
  CHECK_FALSE(validate(SetPlaybackSpeed{DeckId::A, 1.0}, empty).has_value());
  CHECK_FALSE(validate(SetPlaybackSpeed{DeckId::A, limits::kSpeedMin}, empty).has_value());
  CHECK_FALSE(validate(SetPlaybackSpeed{DeckId::A, limits::kSpeedMax}, empty).has_value());
  CHECK(validate(SetPlaybackSpeed{DeckId::A, 0.49}, empty)->code == CommandErrorCode::OutOfRange);
  CHECK(validate(SetPlaybackSpeed{DeckId::A, 2.01}, empty)->code == CommandErrorCode::OutOfRange);
  CHECK(validate(SetPlaybackSpeed{DeckId::A, 0.0}, empty)->code == CommandErrorCode::OutOfRange);
  CHECK(validate(SetPlaybackSpeed{DeckId::A, kNaN}, empty)->code == CommandErrorCode::NotFinite);
}

TEST_CASE("SetLoop validation requires a track, in-range points and end after start when active") {
  const AppState loaded = stateWithTrackOnA();
  CHECK(validate(SetLoop{DeckId::B, 0.0, 1.0, true}, loaded)->code == CommandErrorCode::NoTrackLoaded);
  CHECK_FALSE(validate(SetLoop{DeckId::A, 1.0, 2.0, true}, loaded).has_value());
  CHECK(validate(SetLoop{DeckId::A, 2.0, 2.0, true}, loaded)->code == CommandErrorCode::OutOfRange);
  CHECK(validate(SetLoop{DeckId::A, 3.0, 2.0, true}, loaded)->code == CommandErrorCode::OutOfRange);
  CHECK(validate(SetLoop{DeckId::A, -1.0, 2.0, true}, loaded)->code == CommandErrorCode::OutOfRange);
  CHECK(validate(SetLoop{DeckId::A, 0.0, kNaN, true}, loaded)->code == CommandErrorCode::NotFinite);
  CHECK(validate(SetLoop{DeckId::A, kNaN, 1.0, false}, loaded)->code == CommandErrorCode::NotFinite);
  // An inactive loop keeps the region, so an inverted one is tolerated.
  CHECK_FALSE(validate(SetLoop{DeckId::A, 3.0, 2.0, false}, loaded).has_value());
}

TEST_CASE("Sync and SeparateStems need a loaded track; SetRecording needs nothing") {
  const AppState loaded = stateWithTrackOnA();
  const AppState empty;
  CHECK_FALSE(validate(Sync{DeckId::A}, loaded).has_value());
  CHECK_FALSE(validate(SeparateStems{DeckId::A}, loaded).has_value());
  CHECK(validate(Sync{DeckId::A}, empty)->code == CommandErrorCode::NoTrackLoaded);
  CHECK(validate(SeparateStems{DeckId::A}, empty)->code == CommandErrorCode::NoTrackLoaded);
  CHECK_FALSE(validate(SetRecording{true}, empty).has_value());
  CHECK_FALSE(validate(SetRecording{false}, empty).has_value());
}

TEST_CASE("apply stores playback speed and loop on the addressed deck only") {
  const AppState loaded = stateWithTrackOnA();

  const AppState sped = apply(loaded, SetPlaybackSpeed{DeckId::A, 1.25});
  CHECK(sped.deck(DeckId::A).playbackSpeed == 1.25);
  CHECK(sped.deck(DeckId::B).playbackSpeed == 1.0);
  CHECK(sped.revision == loaded.revision + 1);

  const AppState looped = apply(sped, SetLoop{DeckId::A, 1.0, 3.0, true});
  CHECK(looped.deck(DeckId::A).loop == LoopState{true, 1.0, 3.0});
  CHECK(looped.deck(DeckId::A).playbackSpeed == 1.25);
  CHECK(looped.deck(DeckId::B).loop == LoopState{});

  const AppState off = apply(looped, SetLoop{DeckId::A, 1.0, 3.0, false});
  CHECK_FALSE(off.deck(DeckId::A).loop.active);
  CHECK(off.deck(DeckId::A).loop.startSeconds == 1.0);
  CHECK(off.deck(DeckId::A).loop.endSeconds == 3.0);
}

TEST_CASE("apply SetRecording toggles the recording flag") {
  const AppState on = apply(AppState{}, SetRecording{true});
  CHECK(on.recording);
  CHECK_FALSE(apply(on, SetRecording{false}).recording);
}

TEST_CASE("Seek, Sync and SeparateStems only bump the revision (engine facts are telemetry)") {
  const AppState loaded = stateWithTrackOnA();
  for (const Command& command : {Command{Seek{DeckId::A, 5.0}}, Command{Sync{DeckId::A}},
                                 Command{SeparateStems{DeckId::A}}}) {
    const AppState next = apply(loaded, command);
    CHECK(next.revision == loaded.revision + 1);
    CHECK(next.decks == loaded.decks);
  }
}

TEST_CASE("LoadTrack and UnloadTrack reset the loop") {
  AppState state = stateWithTrackOnA();
  state = apply(state, SetLoop{DeckId::A, 1.0, 2.0, true});
  REQUIRE(state.deck(DeckId::A).loop.active);

  const AppState reloaded = apply(state, LoadTrack{DeckId::A, TrackId{7}});
  CHECK(reloaded.deck(DeckId::A).loop == LoopState{});
  CHECK(reloaded.deck(DeckId::A).track == TrackId{7});

  const AppState unloaded = apply(state, UnloadTrack{DeckId::A});
  CHECK(unloaded.deck(DeckId::A).loop == LoopState{});
  CHECK_FALSE(unloaded.deck(DeckId::A).hasTrack());
}
