// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "Core/MIDI/MidiTypes.hpp"
#include "MIDI/MidiDeviceManager.hpp"

using namespace zyron;

TEST_CASE("MidiDeviceManager: Device enumeration and message routing (P9-01)", "[midi][devices]") {
  midi::MidiDeviceManager manager;

  SECTION("Hardware device list query returns without throwing") {
    const auto inputs = manager.availableInputs();
    const auto outputs = manager.availableOutputs();
    // In headless / CI test environment, zero devices is valid and normal
    CHECK(manager.activeInputCount() == 0);
    CHECK(manager.activeOutputCount() == 0);
  }

  SECTION("Incoming MIDI event handling dispatches to registered listener") {
    core::MidiEvent receivedEvent;
    bool eventFired = false;

    manager.setMidiCallback([&](const core::MidiEvent& evt) {
      receivedEvent = evt;
      eventFired = true;
    });

    core::MidiEvent testEvent;
    testEvent.type = core::MidiMessageType::ControlChange;
    testEvent.channel = 1;
    testEvent.number = 19;
    testEvent.value = 127;
    testEvent.timestampSec = 1.234;

    manager.handleIncomingMidi(testEvent);

    CHECK(eventFired);
    CHECK(receivedEvent.type == core::MidiMessageType::ControlChange);
    CHECK(receivedEvent.channel == 1);
    CHECK(receivedEvent.number == 19);
    CHECK(receivedEvent.value == 127);
    CHECK(receivedEvent.normalizedValue() == 1.0f);
  }

  SECTION("Sending MIDI message returns false gracefully on non-existent hardware") {
    core::MidiEvent testEvent;
    testEvent.type = core::MidiMessageType::NoteOn;
    testEvent.channel = 1;
    testEvent.number = 60;
    testEvent.value = 127;

    CHECK_FALSE(manager.sendMidi(testEvent, "non_existent_device_id"));
  }

  SECTION("Close all inputs and outputs succeeds without error") {
    manager.closeAllInputs();
    manager.closeAllOutputs();
    CHECK(manager.activeInputCount() == 0);
    CHECK(manager.activeOutputCount() == 0);
  }
}
