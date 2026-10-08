// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "Audio/Bridge/CommandBridge.hpp"
#include "Audio/DSP/ChannelStrip.hpp"
#include "Audio/DSP/Mixer.hpp"
#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/SyncManager.hpp"
#include "Audio/Routing/CueRouter.hpp"
#include "Audio/Routing/StemRouter.hpp"
#include "Core/Audio/AudioTap.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Comprehensive 4-deck Realtime Audio Graph (SPEC sections 11, 13, 20, 44, 45, 62).
///
/// Integrates:
///  - 4 Decks (A, B, C, D) with sample-accurate playback and 4-stem mixing
///  - 4 Channel strips (trim gain, 3-band Linkwitz-Riley EQ, DJ filter, FX)
///  - 4-Channel Mixer (crossfader with A,C->Left and B,D->Right defaults, curves, limiter)
///  - Stem router (cross-track stem assignment to mixer channels)
///  - Headphone cue bus and master routing (CueRouter)
///  - Lock-free command drainage and 60 Hz telemetry publisher (CommandBridge)
///  - Master recording tap interface (IAudioTap)
///  - Completely allocation-free and lock-free in audio render (ScopedRealtimeGuard safe).
class AudioGraph {
 public:
  static constexpr int kMaxBlockSize = 4096;

  AudioGraph();
  ~AudioGraph() = default;

  AudioGraph(const AudioGraph&) = delete;
  AudioGraph& operator=(const AudioGraph&) = delete;

  /// Prepares all decks, strips, mixer and routers for audio rendering. Non-RT thread.
  void prepare(double sampleRate) noexcept;

  /// Resets audio state and clearing buffers. Non-RT thread.
  void reset() noexcept;

  // Deck & Channel accessors
  [[nodiscard]] DeckPlayer& deck(core::DeckId id) noexcept {
    return decks_[core::index(id)];
  }
  [[nodiscard]] const DeckPlayer& deck(core::DeckId id) const noexcept {
    return decks_[core::index(id)];
  }
  [[nodiscard]] DeckPlayer& deck(std::size_t index) noexcept {
    return decks_.at(index);
  }
  [[nodiscard]] const DeckPlayer& deck(std::size_t index) const noexcept {
    return decks_.at(index);
  }

  [[nodiscard]] ChannelStrip& channel(core::DeckId id) noexcept {
    return channels_[core::index(id)];
  }
  [[nodiscard]] const ChannelStrip& channel(core::DeckId id) const noexcept {
    return channels_[core::index(id)];
  }
  [[nodiscard]] ChannelStrip& channel(std::size_t index) noexcept {
    return channels_.at(index);
  }
  [[nodiscard]] const ChannelStrip& channel(std::size_t index) const noexcept {
    return channels_.at(index);
  }

  // Subsystems
  [[nodiscard]] Mixer& mixer() noexcept { return mixer_; }
  [[nodiscard]] const Mixer& mixer() const noexcept { return mixer_; }

  [[nodiscard]] CueRouter& cueRouter() noexcept { return cueRouter_; }
  [[nodiscard]] const CueRouter& cueRouter() const noexcept { return cueRouter_; }

  [[nodiscard]] StemRouter& stemRouter() noexcept { return stemRouter_; }
  [[nodiscard]] const StemRouter& stemRouter() const noexcept { return stemRouter_; }

  [[nodiscard]] SyncManager& syncManager() noexcept { return syncManager_; }
  [[nodiscard]] const SyncManager& syncManager() const noexcept { return syncManager_; }

  [[nodiscard]] CommandBridge& bridge() noexcept { return bridge_; }
  [[nodiscard]] const CommandBridge& bridge() const noexcept { return bridge_; }

  // Recording tap
  void setMasterTap(core::IAudioTap* tap) noexcept { masterTap_.store(tap, std::memory_order_release); }
  [[nodiscard]] core::IAudioTap* masterTap() const noexcept {
    return masterTap_.load(std::memory_order_acquire);
  }

  /// Test hook: stretcher restarts that happened in the last / any 4096-frame chunk (the budget keeps them at <= 1).
  [[nodiscard]] int lastBlockPrimes() const noexcept { return lastBlockPrimes_.load(std::memory_order_relaxed); }
  [[nodiscard]] int maxBlockPrimes() const noexcept { return maxBlockPrimes_.load(std::memory_order_relaxed); }

  /// Realtime rendering callback. Overwrites output channels (up to 4 channels: Master L/R, Cue L/R).
  /// Strictly realtime safe: zero memory allocations, zero locks, zero exceptions.
  void render(float* const* outputs, int numChannels, int numSamples) noexcept;

 private:
  void resetAudioState() noexcept;  // everything except the command queue
  void drainMessages() noexcept;
  void updateTelemetry() noexcept;

  double sampleRate_{48000.0};

  std::array<DeckPlayer, core::kDeckCount> decks_;
  std::array<ChannelStrip, core::kDeckCount> channels_;
  Mixer mixer_;
  CueRouter cueRouter_;
  StemRouter stemRouter_;
  SyncManager syncManager_;
  CommandBridge bridge_;
  std::atomic<core::IAudioTap*> masterTap_{nullptr};
  /// A synced start waiting for its Play (DeckPhaseLock message). Audio thread only. The grids travel in the message, so
  /// the audio thread never reads state another thread writes; `track` ties it to the track it was made for.
  struct PendingPhaseLock {
    bool armed{false};
    std::size_t master{0};
    DeckGrid target;
    DeckGrid masterGrid;
    const TrackBuffer* track{nullptr};
  };
  std::array<PendingPhaseLock, core::kDeckCount> pendingLock_{};

  // Preallocated planar scratch buffers for RT rendering (zero heap allocations)
  // 4 decks * 2 channels (L, R)
  std::array<std::array<float, kMaxBlockSize>, core::kDeckCount * 2> deckScratch_{};
  std::array<float, kMaxBlockSize> masterL_{};
  std::array<float, kMaxBlockSize> masterR_{};
  std::array<float, kMaxBlockSize> cueL_{};
  std::array<float, kMaxBlockSize> cueR_{};

  std::size_t primeTurn_{0};  // deck that is offered the stretcher-prime budget first in the next block
  std::atomic<int> lastBlockPrimes_{0};
  std::atomic<int> maxBlockPrimes_{0};
  std::atomic<std::uint64_t> callbackCount_{0};
};

}  // namespace zyron::audio
