// SPDX-License-Identifier: AGPL-3.0-only
// Commands that shape the DJ sound: keylock, key shift, channel FX, track gain trim, master processing.
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

#include "Core/Commands/Command.hpp"

using namespace zyron::core;

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}  // namespace

TEST_CASE("DJ-sound commands have wire names and the right targets", "[core][command][dj-sound]") {
  CHECK(commandName(SetKeylock{DeckId::A, true}) == "SET_KEYLOCK");
  CHECK(commandName(SetKeyShift{DeckId::A, 1.0F}) == "SET_KEY_SHIFT");
  CHECK(commandName(SetFx{DeckId::A}) == "SET_FX");
  CHECK(commandName(SetFxTempo{DeckId::A, 0.5}) == "SET_FX_TEMPO");
  CHECK(commandName(SetTrackGainTrim{DeckId::A, 0.0F}) == "SET_TRACK_GAIN_TRIM");
  CHECK(commandName(SetMasterProcessing{}) == "SET_MASTER_PROCESSING");

  CHECK(targetDeck(SetKeylock{DeckId::B, true}) == DeckId::B);
  CHECK(targetDeck(SetFx{DeckId::C}) == DeckId::C);
  CHECK(targetDeck(SetTrackGainTrim{DeckId::D, 1.0F}) == DeckId::D);
  CHECK_FALSE(targetDeck(SetMasterProcessing{}).has_value());
}

TEST_CASE("a default deck has keylock on and no key shift, trim or effects", "[core][state][dj-sound]") {
  const AppState state;
  for (const auto& deck : state.decks) {
    CHECK(deck.keylock);
    CHECK(deck.keyShift == 0.0F);
    CHECK(deck.trackGainTrimDb == 0.0F);
    CHECK(deck.fxBeatSeconds == 0.0);
    for (const auto& slot : deck.fx) {
      CHECK(slot.type == FxType::None);
      CHECK_FALSE(slot.enabled);
    }
  }
  CHECK(state.masterProcessing.glue);
  CHECK(state.masterProcessing.limiter);
}

TEST_CASE("DJ-sound commands validate their ranges", "[core][command][dj-sound]") {
  const AppState state;
  CHECK_FALSE(validate(SetKeylock{DeckId::A, false}, state).has_value());  // no track needed
  CHECK_FALSE(validate(SetKeyShift{DeckId::A, -6.0F}, state).has_value());
  CHECK_FALSE(validate(SetKeyShift{DeckId::A, 6.0F}, state).has_value());
  CHECK_FALSE(validate(SetKeyShift{DeckId::A, 2.5F}, state).has_value());

  struct Row {
    Command command;
    CommandErrorCode expected;
  };
  const std::vector<Row> rows{
      {SetKeyShift{DeckId::A, 6.1F}, CommandErrorCode::OutOfRange},
      {SetKeyShift{DeckId::A, -6.1F}, CommandErrorCode::OutOfRange},
      {SetKeyShift{DeckId::A, kNaN}, CommandErrorCode::NotFinite},
      {SetTrackGainTrim{DeckId::A, 12.5F}, CommandErrorCode::OutOfRange},
      {SetTrackGainTrim{DeckId::A, -12.5F}, CommandErrorCode::OutOfRange},
      {SetTrackGainTrim{DeckId::A, kNaN}, CommandErrorCode::NotFinite},
      {SetFx{DeckId::A, 2, FxType::Echo}, CommandErrorCode::OutOfRange},
      {SetFx{DeckId::A, -1, FxType::Echo}, CommandErrorCode::OutOfRange},
      {SetFx{DeckId::A, 0, static_cast<FxType>(99)}, CommandErrorCode::OutOfRange},
      {SetFx{DeckId::A, 0, FxType::Echo, true, 1.5F, 0.5F}, CommandErrorCode::OutOfRange},
      {SetFx{DeckId::A, 0, FxType::Echo, true, 0.5F, -0.1F}, CommandErrorCode::OutOfRange},
      {SetFx{DeckId::A, 0, FxType::Echo, true, kNaN, 0.5F}, CommandErrorCode::NotFinite},
      {SetFxTempo{DeckId::A, 0.0}, CommandErrorCode::OutOfRange},
      {SetFxTempo{DeckId::A, 10.0}, CommandErrorCode::OutOfRange},
      {SetKeylock{static_cast<DeckId>(9), true}, CommandErrorCode::InvalidDeck},
  };
  for (const auto& row : rows) {
    const auto error = validate(row.command, state);
    INFO(commandName(row.command));
    REQUIRE(error.has_value());
    CHECK(error->code == row.expected);
  }
  CHECK_FALSE(validate(SetFx{DeckId::D, 1, FxType::Delay, true, 1.0F, 0.0F}, state).has_value());
  CHECK_FALSE(validate(SetFxTempo{DeckId::A, 60.0 / 174.0}, state).has_value());
  CHECK_FALSE(validate(SetMasterProcessing{false, false}, state).has_value());
}

TEST_CASE("DJ-sound commands update only the addressed deck", "[core][command][dj-sound]") {
  AppState state;
  state = apply(state, SetKeylock{DeckId::B, false});
  state = apply(state, SetKeyShift{DeckId::B, -2.5F});
  state = apply(state, SetTrackGainTrim{DeckId::B, -4.0F});
  state = apply(state, SetFxTempo{DeckId::B, 0.345});
  state = apply(state, SetFx{DeckId::B, 1, FxType::Reverb, true, 0.3F, 0.8F, false});

  const DeckState& b = state.deck(DeckId::B);
  CHECK_FALSE(b.keylock);
  CHECK(b.keyShift == -2.5F);
  CHECK(b.trackGainTrimDb == -4.0F);
  CHECK(b.fxBeatSeconds == 0.345);
  CHECK(b.fx[0].type == FxType::None);
  CHECK(b.fx[1].type == FxType::Reverb);
  CHECK(b.fx[1].enabled);
  CHECK(b.fx[1].wet == 0.3F);
  CHECK(b.fx[1].param == 0.8F);
  CHECK_FALSE(b.fx[1].tailAfterFader);

  const DeckState& a = state.deck(DeckId::A);
  CHECK(a.keylock);
  CHECK(a.keyShift == 0.0F);
  CHECK(a.fx[1].type == FxType::None);
  CHECK(state.revision == 5);
}

TEST_CASE("SetFx with FxType::None empties the slot and reports it disabled", "[core][command][dj-sound]") {
  AppState state = apply(AppState{}, SetFx{DeckId::A, 0, FxType::Echo, true, 0.5F, 0.5F});
  REQUIRE(state.deck(DeckId::A).fx[0].enabled);
  state = apply(state, SetFx{DeckId::A, 0, FxType::None, true, 0.5F, 0.5F});
  CHECK(state.deck(DeckId::A).fx[0].type == FxType::None);
  CHECK_FALSE(state.deck(DeckId::A).fx[0].enabled);
}

TEST_CASE("SetMasterProcessing is mirrored in the state", "[core][command][dj-sound]") {
  AppState state = apply(AppState{}, SetMasterProcessing{false, true});
  CHECK_FALSE(state.masterProcessing.glue);
  CHECK(state.masterProcessing.limiter);
  state = apply(state, SetMasterProcessing{true, false});
  CHECK(state.masterProcessing.glue);
  CHECK_FALSE(state.masterProcessing.limiter);
}
