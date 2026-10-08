// SPDX-License-Identifier: AGPL-3.0-only
#include "MIDI/HidManager.hpp"

#include <algorithm>

namespace zyron::midi {

HidManager::HidManager(core::CommandBus& commandBus)
    : commandBus_(&commandBus), profile_(createRadioMasterBoxerProfile()) {}

std::optional<core::Command> HidManager::translateEvent(const core::HidEvent& event) const {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto& entry : profile_.mappings) {
    if (entry.elementIndex == event.elementIndex && entry.type == event.type) {
      const auto deck = entry.deck.value_or(core::DeckId::A);

      if (entry.type == core::HidElementType::Button) {
        if (event.value > 0.5f) {
          if (entry.targetCommandName == "PLAY") return core::Play{deck};
          if (entry.targetCommandName == "PAUSE") return core::Pause{deck};
          if (entry.targetCommandName == "CUE") return core::Cue{deck};
          if (entry.targetCommandName == "SET_STEM_MUTE") {
            return core::SetStemMute{deck, entry.stem.value_or(core::StemKind::Vocals), true};
          }
        } else {
          if (entry.targetCommandName == "SET_STEM_MUTE") {
            return core::SetStemMute{deck, entry.stem.value_or(core::StemKind::Vocals), false};
          }
        }
      } else if (entry.type == core::HidElementType::Axis) {
        // Event value is in [-1.0, 1.0]
        float norm = (event.value + 1.0f) * 0.5f;  // 0.0 .. 1.0
        if (entry.invert) {
          norm = 1.0f - norm;
        }

        const float param = entry.minParamValue + norm * (entry.maxParamValue - entry.minParamValue);

        if (entry.targetCommandName == "SET_VOLUME") {
          return core::SetVolume{deck, std::clamp(param, 0.0f, 1.0f)};
        }
        if (entry.targetCommandName == "SET_CROSSFADER") {
          // Bipolar: -1.0 to 1.0
          float cf = event.value;
          if (entry.invert) cf = -cf;
          return core::SetCrossfader{std::clamp(cf, -1.0f, 1.0f)};
        }
        if (entry.targetCommandName == "SET_EQ") {
          return core::SetEq{deck, entry.eqBand.value_or(core::EqBand::Low), std::clamp(param, core::limits::kEqMinDb, core::limits::kEqMaxDb)};
        }
        if (entry.targetCommandName == "SET_STEM_VOLUME") {
          return core::SetStemVolume{deck, entry.stem.value_or(core::StemKind::Vocals), std::clamp(param, 0.0f, 1.0f)};
        }
      }
    }
  }

  return std::nullopt;
}

bool HidManager::processAndSubmit(const core::HidEvent& event) {
  const auto cmd = translateEvent(event);
  if (!cmd.has_value() || !commandBus_) {
    return false;
  }

  core::CommandOrigin origin;
  origin.kind = core::CommandOrigin::Kind::Midi;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    origin.sourceId = "HID:" + profile_.controllerModel;
  }

  const auto err = commandBus_->submit(*cmd, origin);
  return !err.has_value();
}

core::HidProfile HidManager::createRadioMasterBoxerProfile() {
  core::HidProfile p;
  p.version = 1;
  p.profileName = "RadioMaster Boxer RC Console";
  p.controllerModel = "RadioMaster Boxer";
  p.description = "Maps Boxer gimbals to faders/crossfader, 2-position switches to transport and mute.";

  // Left Gimbal: Y axis (Element 0) = Deck A Volume, X axis (Element 1) = Crossfader
  p.mappings.push_back({ 0, core::HidElementType::Axis, "SET_VOLUME", core::DeckId::A, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ 1, core::HidElementType::Axis, "SET_CROSSFADER", std::nullopt, std::nullopt, std::nullopt, -1.0f, 1.0f, false });

  // Right Gimbal: Y axis (Element 2) = Deck B Volume
  p.mappings.push_back({ 2, core::HidElementType::Axis, "SET_VOLUME", core::DeckId::B, std::nullopt, std::nullopt, 0.0f, 1.0f, false });

  // Switches: SA (Button 0) = Play Deck A, SB (Button 1) = Play Deck B
  p.mappings.push_back({ 0, core::HidElementType::Button, "PLAY", core::DeckId::A, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ 1, core::HidElementType::Button, "PLAY", core::DeckId::B, std::nullopt, std::nullopt, 0.0f, 1.0f, false });

  // Pots: S1 (Element 3) = Deck A Low EQ, S2 (Element 4) = Deck B Low EQ
  p.mappings.push_back({ 3, core::HidElementType::Axis, "SET_EQ", core::DeckId::A, core::EqBand::Low, std::nullopt, -60.0f, 12.0f, false });
  p.mappings.push_back({ 4, core::HidElementType::Axis, "SET_EQ", core::DeckId::B, core::EqBand::Low, std::nullopt, -60.0f, 12.0f, false });

  return p;
}

core::HidProfile HidManager::createStandardGamepadProfile() {
  core::HidProfile p;
  p.version = 1;
  p.profileName = "Standard DualSense / Xbox Gamepad";
  p.controllerModel = "Generic Gamepad";
  p.description = "Gamepad triggers map to volume faders, D-Pad/face buttons to Play/Cue.";

  // Face Buttons: A/Cross (Button 0) = Play Deck A, B/Circle (Button 1) = Play Deck B
  p.mappings.push_back({ 0, core::HidElementType::Button, "PLAY", core::DeckId::A, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ 1, core::HidElementType::Button, "PLAY", core::DeckId::B, std::nullopt, std::nullopt, 0.0f, 1.0f, false });

  // Triggers: L2 (Axis 0) = Deck A Volume, R2 (Axis 1) = Deck B Volume
  p.mappings.push_back({ 0, core::HidElementType::Axis, "SET_VOLUME", core::DeckId::A, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ 1, core::HidElementType::Axis, "SET_VOLUME", core::DeckId::B, std::nullopt, std::nullopt, 0.0f, 1.0f, false });

  // Left Stick X (Axis 2) = Crossfader
  p.mappings.push_back({ 2, core::HidElementType::Axis, "SET_CROSSFADER", std::nullopt, std::nullopt, std::nullopt, -1.0f, 1.0f, false });

  return p;
}

}  // namespace zyron::midi
