// SPDX-License-Identifier: AGPL-3.0-only
#include "MIDI/MidiLearnManager.hpp"

#include <algorithm>

namespace zyron::midi {

void MidiLearnManager::startLearn(core::MidiLearnTarget target) {
  std::lock_guard<std::mutex> lock(mutex_);
  isLearning_ = true;
  currentTarget_ = std::move(target);
}

void MidiLearnManager::cancelLearn() {
  std::lock_guard<std::mutex> lock(mutex_);
  isLearning_ = false;
  currentTarget_.reset();
}

bool MidiLearnManager::isLearning() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return isLearning_;
}

std::optional<core::MidiLearnTarget> MidiLearnManager::activeTarget() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return currentTarget_;
}

bool MidiLearnManager::processIncomingMidi(const core::MidiEvent& event, MidiMapper& mapper) {
  LearnCompleteCallback cb;
  core::MidiMappingEntry newEntry;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isLearning_ || !currentTarget_) {
      return false;
    }

    // Only map CC, NoteOn, or PitchBend
    if (event.type == core::MidiMessageType::NoteOff) {
      return false;
    }

    newEntry.id = "learned_" + currentTarget_->commandName + "_" + std::to_string(event.channel) + "_" + std::to_string(event.number);
    newEntry.messageType = event.type;
    newEntry.channel = event.channel;
    newEntry.number = event.number;
    newEntry.mode = core::MidiMappingMode::Absolute;
    newEntry.targetCommandName = currentTarget_->commandName;
    newEntry.deck = currentTarget_->deck;
    newEntry.eqBand = currentTarget_->eqBand;
    newEntry.stem = currentTarget_->stem;
    newEntry.minParamValue = currentTarget_->minValue;
    newEntry.maxParamValue = currentTarget_->maxValue;

    // Conclude learning
    isLearning_ = false;
    currentTarget_.reset();
    cb = onLearned_;
  }

  // Update mapper profile
  auto profile = mapper.currentProfile();

  // Replace existing mapping on same channel/number if present
  auto it = std::find_if(profile.mappings.begin(), profile.mappings.end(), [&](const auto& m) {
    return m.channel == newEntry.channel && m.number == newEntry.number && m.messageType == newEntry.messageType;
  });

  if (it != profile.mappings.end()) {
    *it = newEntry;
  } else {
    profile.mappings.push_back(newEntry);
  }

  mapper.setProfile(std::move(profile));

  if (cb) {
    cb(newEntry);
  }

  return true;
}

}  // namespace zyron::midi
