// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <variant>

#include "Core/Commands/CommandBus.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/MIDI/HidTypes.hpp"
#include "Core/State/AppState.hpp"
#include "MIDI/HidManager.hpp"

using namespace zyron;

namespace {

struct Fixture {
  core::StateStore store;
  core::EventBus events;
  core::CommandBus bus{store, events};
  midi::HidManager hid{bus};
};

}  // namespace

TEST_CASE("HidManager: RadioMaster Boxer and Gamepad HID mapping (SPEC section 49, P9-04)", "[midi][hid]") {
  Fixture f;

  SECTION("RadioMaster Boxer gimbal axis translates to Deck A Volume and Crossfader") {
    f.hid.setProfile(midi::HidManager::createRadioMasterBoxerProfile());

    // Left Gimbal Y axis centered (0.0): maps to volume 0.5
    core::HidEvent gimbalY;
    gimbalY.type = core::HidElementType::Axis;
    gimbalY.elementIndex = 0;
    gimbalY.value = 0.0f;

    const auto cmdY = f.hid.translateEvent(gimbalY);
    REQUIRE(cmdY.has_value());
    REQUIRE(std::holds_alternative<core::SetVolume>(*cmdY));
    const auto& vol = std::get<core::SetVolume>(*cmdY);
    CHECK(vol.deck == core::DeckId::A);
    CHECK(std::abs(vol.linear - 0.5f) < 0.01f);

    // Left Gimbal X axis fully right (1.0): maps to crossfader +1.0
    core::HidEvent gimbalX;
    gimbalX.type = core::HidElementType::Axis;
    gimbalX.elementIndex = 1;
    gimbalX.value = 1.0f;

    const auto cmdX = f.hid.translateEvent(gimbalX);
    REQUIRE(cmdX.has_value());
    REQUIRE(std::holds_alternative<core::SetCrossfader>(*cmdX));
    CHECK(std::get<core::SetCrossfader>(*cmdX).position == 1.0f);
  }

  SECTION("RadioMaster Boxer physical switches trigger transport") {
    f.hid.setProfile(midi::HidManager::createRadioMasterBoxerProfile());

    core::HidEvent switchA;
    switchA.type = core::HidElementType::Button;
    switchA.elementIndex = 0;
    switchA.value = 1.0f;  // Switch flipped

    const auto cmd = f.hid.translateEvent(switchA);
    REQUIRE(cmd.has_value());
    REQUIRE(std::holds_alternative<core::Play>(*cmd));
    CHECK(std::get<core::Play>(*cmd).deck == core::DeckId::A);
  }

  SECTION("Standard Gamepad profile translates buttons and trigger axes") {
    f.hid.setProfile(midi::HidManager::createStandardGamepadProfile());

    // Button 0 (A/Cross) -> Play Deck A
    core::HidEvent btnA;
    btnA.type = core::HidElementType::Button;
    btnA.elementIndex = 0;
    btnA.value = 1.0f;

    const auto cmdPlay = f.hid.translateEvent(btnA);
    REQUIRE(cmdPlay.has_value());
    REQUIRE(std::holds_alternative<core::Play>(*cmdPlay));
    CHECK(std::get<core::Play>(*cmdPlay).deck == core::DeckId::A);

    // Trigger L2 (Axis 0) -> Deck A Volume
    core::HidEvent triggerL2;
    triggerL2.type = core::HidElementType::Axis;
    triggerL2.elementIndex = 0;
    triggerL2.value = 1.0f;  // Fully pressed

    const auto cmdVol = f.hid.translateEvent(triggerL2);
    REQUIRE(cmdVol.has_value());
    REQUIRE(std::holds_alternative<core::SetVolume>(*cmdVol));
    CHECK(std::get<core::SetVolume>(*cmdVol).deck == core::DeckId::A);
    CHECK(std::get<core::SetVolume>(*cmdVol).linear == 1.0f);
  }

  SECTION("Process and submit to CommandBus alters state") {
    (void)f.bus.submit(core::LoadTrack{core::DeckId::A, core::TrackId{1}}, core::CommandOrigin{core::CommandOrigin::Kind::Ui});
    f.hid.setProfile(midi::HidManager::createRadioMasterBoxerProfile());

    core::HidEvent gimbalY;
    gimbalY.type = core::HidElementType::Axis;
    gimbalY.elementIndex = 0;
    gimbalY.value = -1.0f;  // Bottom -> 0.0 volume

    CHECK(f.hid.processAndSubmit(gimbalY));
    CHECK(f.store.snapshot()->decks[core::index(core::DeckId::A)].volume == 0.0f);
  }
}
