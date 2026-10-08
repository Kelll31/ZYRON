// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <variant>

#include "Core/Commands/CommandBus.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/MIDI/MidiTypes.hpp"
#include "Core/State/AppState.hpp"
#include "MIDI/MidiMapper.hpp"

using namespace zyron;

namespace {

struct Fixture {
  core::StateStore store;
  core::EventBus events;
  core::CommandBus bus{store, events};
  midi::MidiMapper mapper{bus};
};

}  // namespace

TEST_CASE("MidiMapper: Event translation to typed Commands (P9-02)", "[midi][mapper]") {
  Fixture f;

  SECTION("NoteOn translates to transport commands") {
    core::MidiEvent playEvt;
    playEvt.type = core::MidiMessageType::NoteOn;
    playEvt.channel = 1;
    playEvt.number = 11;
    playEvt.value = 127;

    const auto cmd = f.mapper.translateEvent(playEvt);
    REQUIRE(cmd.has_value());
    REQUIRE(std::holds_alternative<core::Play>(*cmd));
    CHECK(std::get<core::Play>(*cmd).deck == core::DeckId::A);
  }

  SECTION("CC knob translates to EQ with proper parameter range scaling") {
    core::MidiEvent eqEvt;
    eqEvt.type = core::MidiMessageType::ControlChange;
    eqEvt.channel = 1;
    eqEvt.number = 23;  // Deck A Low EQ: range [-60.0, 12.0]
    eqEvt.value = 127;  // Max position

    const auto cmd = f.mapper.translateEvent(eqEvt);
    REQUIRE(cmd.has_value());
    REQUIRE(std::holds_alternative<core::SetEq>(*cmd));
    const auto& setEq = std::get<core::SetEq>(*cmd);
    CHECK(setEq.deck == core::DeckId::A);
    CHECK(setEq.band == core::EqBand::Low);
    CHECK(setEq.db == 12.0f);
  }

  SECTION("Crossfader maps to center at value 64 / 127") {
    core::MidiEvent cfEvt;
    cfEvt.type = core::MidiMessageType::ControlChange;
    cfEvt.channel = 1;
    cfEvt.number = 31;  // Crossfader: range [-1.0, 1.0]
    cfEvt.value = 64;

    const auto cmd = f.mapper.translateEvent(cfEvt);
    REQUIRE(cmd.has_value());
    REQUIRE(std::holds_alternative<core::SetCrossfader>(*cmd));
    const auto& cf = std::get<core::SetCrossfader>(*cmd);
    CHECK(std::abs(cf.position) < 0.05f);
  }

  SECTION("Process and submit to CommandBus alters state") {
    // Load track onto Deck A first so SetVolume is accepted
    (void)f.bus.submit(core::LoadTrack{core::DeckId::A, core::TrackId{1}}, core::CommandOrigin{core::CommandOrigin::Kind::Ui});

    core::MidiEvent volEvt;
    volEvt.type = core::MidiMessageType::ControlChange;
    volEvt.channel = 1;
    volEvt.number = 19;  // Deck A Volume
    volEvt.value = 0;    // Fader at bottom (0.0)

    CHECK(f.mapper.processAndSubmit(volEvt));

    const auto state = f.store.snapshot();
    CHECK(state->decks[core::index(core::DeckId::A)].volume == 0.0f);
  }
}

TEST_CASE("MidiMapper: Profile JSON export and parse roundtrip (P9-02)", "[midi][mapper][json]") {
  const auto original = midi::MidiMapper::createDefault2DeckProfile();
  const std::string json = midi::MidiMapper::exportProfileJson(original);

  CHECK_FALSE(json.empty());
  CHECK(json.find("Standard 2-Deck MIDI Console") != std::string::npos);

  const auto parsed = midi::MidiMapper::parseProfileJson(json);
  REQUIRE(parsed.has_value());
  CHECK(parsed->version == original.version);
  CHECK(parsed->controllerName == original.controllerName);
  CHECK(parsed->mappings.size() == original.mappings.size());
}
