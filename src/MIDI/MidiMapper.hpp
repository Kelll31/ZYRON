// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/MIDI/MidiTypes.hpp"

namespace zyron::midi {

/// Translates MIDI hardware messages to Command API instructions via versioned JSON profiles (SPEC section 47, ROADMAP P9-02).
class MidiMapper {
 public:
  MidiMapper() = default;
  explicit MidiMapper(core::CommandBus& commandBus);
  ~MidiMapper() = default;

  void setCommandBus(core::CommandBus* bus) { commandBus_ = bus; }

  void setProfile(core::MidiProfile profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    profile_ = std::move(profile);
  }

  [[nodiscard]] core::MidiProfile currentProfile() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return profile_;
  }

  /// Translates an incoming MIDI event according to active profile mappings.
  [[nodiscard]] std::optional<core::Command> translateEvent(const core::MidiEvent& event) const;

  /// Translates and immediately dispatches command to the CommandBus.
  bool processAndSubmit(const core::MidiEvent& event);

  // Profile JSON serialization (SPEC section 47)
  [[nodiscard]] static std::optional<core::MidiProfile> parseProfileJson(std::string_view json);
  [[nodiscard]] static std::string exportProfileJson(const core::MidiProfile& profile);

  bool loadProfileFromFile(const std::filesystem::path& path);
  bool saveProfileToFile(const core::MidiProfile& profile, const std::filesystem::path& path);

  /// Generates factory default mapping for common 2-deck DJ controllers (e.g. DDJ-style).
  [[nodiscard]] static core::MidiProfile createDefault2DeckProfile();

 private:
  core::CommandBus* commandBus_{nullptr};
  mutable std::mutex mutex_;
  core::MidiProfile profile_;
};

}  // namespace zyron::midi
