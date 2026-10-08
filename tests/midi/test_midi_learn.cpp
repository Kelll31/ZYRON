// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <variant>

#include "Core/Commands/CommandBus.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/MIDI/MidiTypes.hpp"
#include "Core/State/AppState.hpp"
#include "MIDI/MidiLearnManager.hpp"
#include "MIDI/MidiMapper.hpp"

using namespace zyron;

namespace {

struct Fixture {
  core::StateStore store;
  core::EventBus events;
  core::CommandBus bus{store, events};
  midi::MidiMapper mapper{bus};
  midi::MidiLearnManager learnManager;
};

}  // namespace

TEST_CASE("MidiLearnManager: Interactive MIDI Learn workflow (SPEC section 48, P9-03)", "[midi][learn]") {
  Fixture f;

  SECTION("Interactive mapping of physical knob to Deck B Mid EQ") {
    CHECK_FALSE(f.learnManager.isLearning());

    core::MidiLearnTarget target;
    target.commandName = "SET_EQ";
    target.deck = core::DeckId::B;
    target.eqBand = core::EqBand::Mid;
    target.minValue = -24.0f;
    target.maxValue = 12.0f;

    f.learnManager.startLearn(target);
    CHECK(f.learnManager.isLearning());
    REQUIRE(f.learnManager.activeTarget().has_value());
    CHECK(f.learnManager.activeTarget()->deck == core::DeckId::B);

    bool callbackFired = false;
    f.learnManager.setOnLearnedCallback([&](const core::MidiMappingEntry& entry) {
      callbackFired = true;
      CHECK(entry.channel == 3);
      CHECK(entry.number == 44);
      CHECK(entry.targetCommandName == "SET_EQ");
    });

    // DJ moves physical knob (sends CC 44 on channel 3)
    core::MidiEvent hardwareKnobMove;
    hardwareKnobMove.type = core::MidiMessageType::ControlChange;
    hardwareKnobMove.channel = 3;
    hardwareKnobMove.number = 44;
    hardwareKnobMove.value = 64;

    const bool consumed = f.learnManager.processIncomingMidi(hardwareKnobMove, f.mapper);
    CHECK(consumed);
    CHECK(callbackFired);
    CHECK_FALSE(f.learnManager.isLearning());

    // Verify that the mapper now translates this physical knob
    core::MidiEvent subsequentMove;
    subsequentMove.type = core::MidiMessageType::ControlChange;
    subsequentMove.channel = 3;
    subsequentMove.number = 44;
    subsequentMove.value = 127;  // Max position

    const auto translated = f.mapper.translateEvent(subsequentMove);
    REQUIRE(translated.has_value());
    REQUIRE(std::holds_alternative<core::SetEq>(*translated));
    const auto& eq = std::get<core::SetEq>(*translated);
    CHECK(eq.deck == core::DeckId::B);
    CHECK(eq.band == core::EqBand::Mid);
    CHECK(eq.db == 12.0f);
  }

  SECTION("Cancel learn session aborts without modifying mappings") {
    core::MidiLearnTarget target;
    target.commandName = "PLAY";
    target.deck = core::DeckId::A;

    f.learnManager.startLearn(target);
    CHECK(f.learnManager.isLearning());

    f.learnManager.cancelLearn();
    CHECK_FALSE(f.learnManager.isLearning());
    CHECK_FALSE(f.learnManager.activeTarget().has_value());
  }
}
