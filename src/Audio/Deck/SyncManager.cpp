// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Deck/SyncManager.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

SyncManager::SyncManager() {
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    syncEnabled_[i].store(false, std::memory_order_relaxed);
  }
}

void SyncManager::setDeckGrid(core::DeckId id, const DeckGrid& grid) noexcept {
  const auto idx = core::index(id);
  if (idx < core::kDeckCount) {
    grids_[idx] = grid;
  }
}

const DeckGrid& SyncManager::getDeckGrid(core::DeckId id) const noexcept {
  const auto idx = core::index(id);
  if (idx < core::kDeckCount) {
    return grids_[idx];
  }
  static const DeckGrid kEmpty;
  return kEmpty;
}

void SyncManager::setMasterDeck(core::DeckId id) noexcept {
  const auto idx = static_cast<int>(core::index(id));
  if (idx >= 0 && idx < static_cast<int>(core::kDeckCount)) {
    masterDeckIndex_.store(idx, std::memory_order_release);
  }
}

std::optional<core::DeckId> SyncManager::masterDeck() const noexcept {
  const int idx = masterDeckIndex_.load(std::memory_order_acquire);
  if (idx >= 0 && idx < static_cast<int>(core::kDeckCount)) {
    return static_cast<core::DeckId>(idx);
  }
  return std::nullopt;
}

void SyncManager::setAutoMasterEnabled(bool enabled) noexcept {
  autoMaster_.store(enabled, std::memory_order_release);
}

bool SyncManager::isAutoMasterEnabled() const noexcept {
  return autoMaster_.load(std::memory_order_acquire);
}

void SyncManager::setSyncEnabled(core::DeckId id, bool enabled) noexcept {
  const auto idx = core::index(id);
  if (idx < core::kDeckCount) {
    syncEnabled_[idx].store(enabled, std::memory_order_release);
  }
}

void SyncManager::toggleSync(core::DeckId id) noexcept {
  const auto idx = core::index(id);
  if (idx < core::kDeckCount) {
    const bool current = syncEnabled_[idx].load(std::memory_order_relaxed);
    syncEnabled_[idx].store(!current, std::memory_order_release);
  }
}

bool SyncManager::isSyncEnabled(core::DeckId id) const noexcept {
  const auto idx = core::index(id);
  if (idx < core::kDeckCount) {
    return syncEnabled_[idx].load(std::memory_order_acquire);
  }
  return false;
}

double SyncManager::calculateTempoMatchSpeed(core::DeckId targetDeck,
                                             core::DeckId masterDeck,
                                             double masterSpeed) const noexcept {
  const auto mIdx = core::index(masterDeck);
  const auto tIdx = core::index(targetDeck);
  if (mIdx >= core::kDeckCount || tIdx >= core::kDeckCount) return 1.0;

  const double masterBpm = grids_[mIdx].bpm;
  const double targetBpm = grids_[tIdx].bpm;
  if (masterBpm <= 0.0 || targetBpm <= 0.0 || masterSpeed <= 0.0) return 1.0;

  const double masterEffectiveBpm = masterBpm * masterSpeed;
  const double rawRatio = masterEffectiveBpm / targetBpm;

  // Resolve octaves (e.g. 174 BPM master vs 87 BPM target or vice-versa)
  // In musical pitch, distance is logarithmic: |log(c)| is symmetric across octaves (0.5x and 2.0x).
  const double candidates[] = {rawRatio * 0.5, rawRatio, rawRatio * 2.0};
  double bestSpeed = rawRatio;
  double minDistance = std::abs(std::log(rawRatio));

  for (double c : candidates) {
    if (c <= 0.0) continue;
    const double dist = std::abs(std::log(c));
    if (dist < minDistance) {
      minDistance = dist;
      bestSpeed = c;
    }
  }

  return bestSpeed;
}

std::int64_t SyncManager::alignPhase(DeckPlayer& targetPlayer,
                                     core::DeckId targetDeck,
                                     const DeckPlayer& masterPlayer,
                                     core::DeckId masterDeck) noexcept {
  const auto mIdx = core::index(masterDeck);
  const auto tIdx = core::index(targetDeck);
  if (mIdx >= core::kDeckCount || tIdx >= core::kDeckCount) return 0;

  const auto& masterGrid = grids_[mIdx];
  const auto& targetGrid = grids_[tIdx];
  const double spbTarget = targetGrid.samplesPerBeat();
  if (spbTarget <= 0.0) return 0;

  const double masterPhase = masterGrid.beatFraction(masterPlayer.currentFrame());
  const double targetPhase = targetGrid.beatFraction(targetPlayer.currentFrame());

  // Difference in beat fractions [-0.5 .. 0.5]
  double diff = targetPhase - masterPhase;
  while (diff > 0.5) diff -= 1.0;
  while (diff < -0.5) diff += 1.0;

  const auto adjustmentSamples = static_cast<std::int64_t>(std::round(-diff * spbTarget));
  const auto newFrame = std::max<std::int64_t>(0, targetPlayer.currentFrame() + adjustmentSamples);

  targetPlayer.seek(newFrame);
  return adjustmentSamples;
}

void SyncManager::syncDeck(DeckPlayer& targetPlayer,
                           core::DeckId targetDeck,
                           const DeckPlayer& masterPlayer,
                           core::DeckId masterDeck) noexcept {
  const double targetSpeed = calculateTempoMatchSpeed(targetDeck, masterDeck, masterPlayer.playbackSpeed());
  targetPlayer.setPlaybackSpeed(targetSpeed);
  alignPhase(targetPlayer, targetDeck, masterPlayer, masterDeck);
}

void SyncManager::update(std::array<DeckPlayer*, core::kDeckCount>& players) noexcept {
  // 1. Auto-master arbitration
  if (autoMaster_.load(std::memory_order_relaxed)) {
    const int currentMasterIdx = masterDeckIndex_.load(std::memory_order_relaxed);
    bool masterPlaying = false;
    if (currentMasterIdx >= 0 && currentMasterIdx < static_cast<int>(core::kDeckCount) && players[currentMasterIdx]) {
      masterPlaying = players[currentMasterIdx]->isPlaying();
    }

    if (!masterPlaying) {
      // Find first playing deck
      for (std::size_t i = 0; i < core::kDeckCount; ++i) {
        if (players[i] && players[i]->isPlaying()) {
          masterDeckIndex_.store(static_cast<int>(i), std::memory_order_release);
          break;
        }
      }
    }
  }

  // 2. Dynamic tempo tracking
  const int masterIdx = masterDeckIndex_.load(std::memory_order_relaxed);
  if (masterIdx < 0 || masterIdx >= static_cast<int>(core::kDeckCount) || !players[masterIdx]) {
    return;
  }

  const double masterSpeed = players[masterIdx]->playbackSpeed();
  const auto mDeck = static_cast<core::DeckId>(masterIdx);

  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    if (static_cast<int>(i) == masterIdx || !players[i]) continue;
    const auto tDeck = static_cast<core::DeckId>(i);

    if (syncEnabled_[i].load(std::memory_order_relaxed)) {
      const double speed = calculateTempoMatchSpeed(tDeck, mDeck, masterSpeed);
      players[i]->setPlaybackSpeed(speed);
    }
  }
}

}  // namespace zyron::audio
