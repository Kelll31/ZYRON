// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "Audio/Deck/DeckPlayer.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Beatgrid definition for a single deck used by the sync engine (§17).
struct DeckGrid {
  double bpm{120.0};
  std::int64_t firstBeatFrame{0};
  int sampleRate{44100};
  int downbeatOffset{0};

  [[nodiscard]] double samplesPerBeat() const noexcept {
    return (bpm > 0.0 && sampleRate > 0) ? (static_cast<double>(sampleRate) * 60.0 / bpm) : 0.0;
  }

  [[nodiscard]] double beatFraction(std::int64_t frame) const noexcept {
    const double spb = samplesPerBeat();
    if (spb <= 0.0) return 0.0;
    const double beatIdx = static_cast<double>(frame - firstBeatFrame) / spb;
    double frac = beatIdx - std::floor(beatIdx);
    return (frac < 0.0) ? (frac + 1.0) : frac;
  }

  [[nodiscard]] std::int64_t snapToNearestBeat(std::int64_t frame) const noexcept {
    const double spb = samplesPerBeat();
    if (spb <= 0.0) return frame;
    const double beatIdx = static_cast<double>(frame - firstBeatFrame) / spb;
    const auto nearestIdx = static_cast<std::int64_t>(std::round(beatIdx));
    return firstBeatFrame + static_cast<std::int64_t>(std::round(static_cast<double>(nearestIdx) * spb));
  }
};

/// Multi-deck Beat Sync engine (SPEC section 17, ROADMAP P3-06).
///
/// Features:
///  - Master deck selection (manual or automatic failover)
///  - BPM matching with octave-aware resolution (e.g. 174 <-> 87 DnB half-time)
///  - Sample-accurate phase alignment
///  - Dynamic tempo tracking (synced decks follow master pitch bends)
///  - Completely allocation-free, realtime-safe.
class SyncManager {
 public:
  SyncManager();
  ~SyncManager() = default;

  SyncManager(const SyncManager&) = delete;
  SyncManager& operator=(const SyncManager&) = delete;

  /// Registers or updates the beatgrid for a deck.
  void setDeckGrid(core::DeckId id, const DeckGrid& grid) noexcept;

  /// Returns the beatgrid for a deck.
  [[nodiscard]] const DeckGrid& getDeckGrid(core::DeckId id) const noexcept;

  // --- Master Deck Control ---

  /// Manually designates a deck as tempo master.
  void setMasterDeck(core::DeckId id) noexcept;

  /// Returns the currently active master deck (if any).
  [[nodiscard]] std::optional<core::DeckId> masterDeck() const noexcept;

  /// Enables or disables automatic master assignment based on playback activity.
  void setAutoMasterEnabled(bool enabled) noexcept;

  [[nodiscard]] bool isAutoMasterEnabled() const noexcept;

  // --- Sync State ---

  /// Enables or disables sync on a deck.
  void setSyncEnabled(core::DeckId id, bool enabled) noexcept;

  /// Toggles sync on a deck.
  void toggleSync(core::DeckId id) noexcept;

  [[nodiscard]] bool isSyncEnabled(core::DeckId id) const noexcept;

  // --- Synchronization Operations (§17) ---

  /// Computes target speed factor to match master's effective BPM, handling octave relationships.
  [[nodiscard]] double calculateTempoMatchSpeed(core::DeckId targetDeck,
                                                core::DeckId masterDeck,
                                                double masterSpeed) const noexcept;

  /// Performs sample-accurate phase alignment on targetDeck to align with masterDeck.
  /// Returns the number of sample frames adjusted on targetDeck.
  std::int64_t alignPhase(DeckPlayer& targetPlayer,
                          core::DeckId targetDeck,
                          const DeckPlayer& masterPlayer,
                          core::DeckId masterDeck) noexcept;

  /// The same alignment with grids handed over explicitly (the audio thread never reads grids_, which the command
  /// thread writes).
  static std::int64_t alignPhaseWith(DeckPlayer& targetPlayer, const DeckGrid& targetGrid,
                                     const DeckPlayer& masterPlayer, const DeckGrid& masterGrid) noexcept;

  /// Full one-shot sync: matches tempo and aligns phase sample-accurately.
  void syncDeck(DeckPlayer& targetPlayer,
                core::DeckId targetDeck,
                const DeckPlayer& masterPlayer,
                core::DeckId masterDeck) noexcept;

  /// Periodic update loop for dynamic tempo tracking and auto-master arbitration.
  void update(std::array<DeckPlayer*, core::kDeckCount>& players) noexcept;

 private:
  std::array<DeckGrid, core::kDeckCount> grids_{};
  std::array<std::atomic<bool>, core::kDeckCount> syncEnabled_{};
  std::atomic<int> masterDeckIndex_{-1};  // -1 = none
  std::atomic<bool> autoMaster_{true};
};

}  // namespace zyron::audio
