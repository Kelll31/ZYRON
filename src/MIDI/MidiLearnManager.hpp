// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "Core/MIDI/MidiTypes.hpp"
#include "MIDI/MidiMapper.hpp"

namespace zyron::midi {

/// Interactive MIDI Learn manager for physical hardware mapping (SPEC section 48, ROADMAP P9-03).
class MidiLearnManager {
 public:
  using LearnCompleteCallback = std::function<void(const core::MidiMappingEntry&)>;

  MidiLearnManager() = default;
  ~MidiLearnManager() = default;

  /// Enters interactive MIDI learn mode targeting a specific UI/Engine parameter.
  void startLearn(core::MidiLearnTarget target);

  /// Cancels active MIDI learn session.
  void cancelLearn();

  [[nodiscard]] bool isLearning() const noexcept;
  [[nodiscard]] std::optional<core::MidiLearnTarget> activeTarget() const;

  /// Inspects an incoming MIDI event. If in learn mode, binds the control to target and returns true.
  bool processIncomingMidi(const core::MidiEvent& event, MidiMapper& mapper);

  void setOnLearnedCallback(LearnCompleteCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onLearned_ = std::move(cb);
  }

 private:
  mutable std::mutex mutex_;
  bool isLearning_{false};
  std::optional<core::MidiLearnTarget> currentTarget_;
  LearnCompleteCallback onLearned_;
};

}  // namespace zyron::midi
