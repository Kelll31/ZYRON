// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/MIDI/HidTypes.hpp"

namespace zyron::midi {

/// Generic Human Interface Device (HID) layer for gamepads, custom HID, and RadioMaster Boxer (SPEC section 49, ROADMAP P9-04).
class HidManager {
 public:
  HidManager() = default;
  explicit HidManager(core::CommandBus& commandBus);
  ~HidManager() = default;

  void setCommandBus(core::CommandBus* bus) { commandBus_ = bus; }

  void setProfile(core::HidProfile profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    profile_ = std::move(profile);
  }

  [[nodiscard]] core::HidProfile currentProfile() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return profile_;
  }

  /// Translates an incoming HID event into a typed Command.
  [[nodiscard]] std::optional<core::Command> translateEvent(const core::HidEvent& event) const;

  /// Translates and dispatches an HID event to the application CommandBus.
  bool processAndSubmit(const core::HidEvent& event);

  /// Factory preset for RadioMaster Boxer RC controller (SPEC section 49).
  [[nodiscard]] static core::HidProfile createRadioMasterBoxerProfile();

  /// Factory preset for standard DualSense / Xbox gamepad (SPEC section 49).
  [[nodiscard]] static core::HidProfile createStandardGamepadProfile();

 private:
  core::CommandBus* commandBus_{nullptr};
  mutable std::mutex mutex_;
  core::HidProfile profile_;
};

}  // namespace zyron::midi
