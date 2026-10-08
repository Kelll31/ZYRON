// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Audio/Effects/Effect.hpp"

namespace zyron::audio {

/// Registry for audio effects (SPEC section 23, ARCHITECTURE section 7).
/// Allows adding new effects without modifying the mixer or audio engine.
class EffectRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Effect>()>;

  static EffectRegistry& instance();

  /// Registers an effect factory under a unique ID.
  void registerEffect(std::string_view id, Factory factory);

  /// Instantiates an effect by ID, or returns nullptr if not found.
  [[nodiscard]] std::unique_ptr<Effect> create(std::string_view id) const;

  /// Returns whether an effect ID is registered.
  [[nodiscard]] bool hasEffect(std::string_view id) const;

  /// Returns all registered effect IDs.
  [[nodiscard]] std::vector<std::string> registeredIds() const;

  /// Registers built-in effects: delay, echo, reverb, flanger, phaser.
  void registerBuiltins();

  /// Clears all registered effects (useful in tests).
  void clear();

 private:
  EffectRegistry() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Factory> factories_;
};

}  // namespace zyron::audio
