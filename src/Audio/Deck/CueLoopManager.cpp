// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Deck/CueLoopManager.hpp"

#include <algorithm>

namespace zyron::audio {

bool CueLoopManager::setHotCue(int index, std::int64_t frame, std::string_view name,
                               std::string_view color, CueType type) {
  if (index < 0 || index >= static_cast<int>(kMaxHotCues)) {
    return false;
  }

  auto& cue = hotCues_[static_cast<std::size_t>(index)];
  cue.index = index;
  cue.frame = std::max<std::int64_t>(0, frame);
  cue.name = std::string(name);
  cue.color = color.empty() ? "#00FF88" : std::string(color);
  cue.type = type;
  cue.active = true;
  return true;
}

bool CueLoopManager::clearHotCue(int index) {
  if (index < 0 || index >= static_cast<int>(kMaxHotCues)) {
    return false;
  }
  hotCues_[static_cast<std::size_t>(index)] = HotCuePoint{index, 0, "", "#00FF88", CueType::Cue, false};
  return true;
}

std::optional<HotCuePoint> CueLoopManager::getHotCue(int index) const {
  if (index < 0 || index >= static_cast<int>(kMaxHotCues)) {
    return std::nullopt;
  }
  const auto& cue = hotCues_[static_cast<std::size_t>(index)];
  if (!cue.active) return std::nullopt;
  return cue;
}

bool CueLoopManager::hasHotCue(int index) const noexcept {
  if (index < 0 || index >= static_cast<int>(kMaxHotCues)) {
    return false;
  }
  return hotCues_[static_cast<std::size_t>(index)].active;
}

bool CueLoopManager::jumpToHotCue(DeckPlayer& player, int index, bool startPlaying) {
  if (index < 0 || index >= static_cast<int>(kMaxHotCues)) {
    return false;
  }
  const auto& cue = hotCues_[static_cast<std::size_t>(index)];
  if (!cue.active) return false;

  player.seek(cue.frame);
  if (startPlaying) {
    player.play();
  }
  return true;
}

void CueLoopManager::setLoopIn(DeckPlayer& player, std::int64_t frame) {
  loopStartFrame_ = std::max<std::int64_t>(0, frame);
  loopInSet_ = true;
  if (loopActive_ && loopEndFrame_ > loopStartFrame_) {
    player.setLoop(loopStartFrame_, loopEndFrame_);
  }
}

void CueLoopManager::setLoopOut(DeckPlayer& player, std::int64_t frame) {
  if (!loopInSet_ || frame <= loopStartFrame_) return;

  loopEndFrame_ = frame;
  loopActive_ = true;
  player.setLoop(loopStartFrame_, loopEndFrame_);
  player.setLoopActive(true);
}

void CueLoopManager::setBeatLoop(DeckPlayer& player, double beats, const DeckGrid& grid) {
  const double clampedBeats = std::clamp(beats, 1.0 / 32.0, 32.0);
  const double spb = grid.samplesPerBeat();
  if (spb <= 0.0) return;

  const auto loopLen = static_cast<std::int64_t>(std::round(clampedBeats * spb));
  if (loopLen <= 0) return;

  const auto curFrame = player.currentFrame();
  const auto start = grid.snapToNearestBeat(curFrame);
  const auto end = start + loopLen;

  loopStartFrame_ = start;
  loopEndFrame_ = end;
  currentLoopBeats_ = clampedBeats;
  loopActive_ = true;
  loopInSet_ = true;

  player.setLoop(start, end);
  player.setLoopActive(true);

  if (curFrame < start || curFrame >= end) {
    player.seek(start);
  }
}

void CueLoopManager::halveLoop(DeckPlayer& player, const DeckGrid& grid) {
  if (!loopActive_ || currentLoopBeats_ <= 1.0 / 32.0) return;

  const double newBeats = currentLoopBeats_ * 0.5;
  const double spb = grid.samplesPerBeat();
  if (spb <= 0.0) return;

  const auto loopLen = static_cast<std::int64_t>(std::round(newBeats * spb));
  loopEndFrame_ = loopStartFrame_ + loopLen;
  currentLoopBeats_ = newBeats;

  player.setLoop(loopStartFrame_, loopEndFrame_);
  if (player.currentFrame() >= loopEndFrame_) {
    player.seek(loopStartFrame_);
  }
}

void CueLoopManager::doubleLoop(DeckPlayer& player, const DeckGrid& grid) {
  if (!loopActive_ || currentLoopBeats_ >= 32.0) return;

  const double newBeats = currentLoopBeats_ * 2.0;
  const double spb = grid.samplesPerBeat();
  if (spb <= 0.0) return;

  const auto loopLen = static_cast<std::int64_t>(std::round(newBeats * spb));
  loopEndFrame_ = loopStartFrame_ + loopLen;
  currentLoopBeats_ = newBeats;

  player.setLoop(loopStartFrame_, loopEndFrame_);
}

void CueLoopManager::reloop(DeckPlayer& player) {
  if (loopEndFrame_ <= loopStartFrame_) return;

  loopActive_ = true;
  player.setLoop(loopStartFrame_, loopEndFrame_);
  player.setLoopActive(true);

  const auto cur = player.currentFrame();
  if (cur < loopStartFrame_ || cur >= loopEndFrame_) {
    player.seek(loopStartFrame_);
  }
}

void CueLoopManager::exitLoop(DeckPlayer& player) {
  loopActive_ = false;
  player.setLoopActive(false);
}

void CueLoopManager::moveLoop(DeckPlayer& player, double beatsDelta, const DeckGrid& grid) {
  if (!loopActive_) return;
  const double spb = grid.samplesPerBeat();
  if (spb <= 0.0) return;

  const auto shift = static_cast<std::int64_t>(std::round(beatsDelta * spb));
  const auto newStart = loopStartFrame_ + shift;
  const auto newEnd = loopEndFrame_ + shift;
  if (newStart < 0) return;

  loopStartFrame_ = newStart;
  loopEndFrame_ = newEnd;
  player.setLoop(loopStartFrame_, loopEndFrame_);

  // Nudge playhead by the same amount so playhead stays in identical loop phase
  const auto newPlayhead = player.currentFrame() + shift;
  player.seek(std::max<std::int64_t>(0, newPlayhead));
}

}  // namespace zyron::audio
