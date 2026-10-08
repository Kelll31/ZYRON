// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "Audio/DSP/Mixer.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Destination mixer channel for a stem source (SPEC section 44, ROADMAP P5-07).
struct StemRouteDestination {
  enum class Type : std::uint8_t {
    DefaultDeckChannel = 0,  // Routed to deck's primary channel
    MixerChannel,            // Routed to an explicit mixer channel index (0..3)
    Disabled                 // Muted / unrouted
  };

  Type type{Type::DefaultDeckChannel};
  int channelIndex{0};

  friend bool operator==(const StemRouteDestination&, const StemRouteDestination&) = default;
};

/// Cross-track stem routing matrix (SPEC section 44, ARCHITECTURE section 7).
///
/// Features:
///  - Maps any stem of any deck (DeckId, StemKind) to any mixer channel (0..3)
///  - Allows multi-track hybrid mixing (e.g. Deck A drums + Deck B vocals into separate strips)
///  - Dynamic routing presets (Default 2-deck, Split Vocals/Beats, Custom Matrix)
///  - Real-time safe, lock-free, zero allocations in audio routing loop.
class StemRouter {
 public:
  static constexpr int kMaxMixerChannels = Mixer::kMaxChannels;

  StemRouter();
  ~StemRouter() = default;

  StemRouter(const StemRouter&) = delete;
  StemRouter& operator=(const StemRouter&) = delete;

  void reset() noexcept;

  /// Assigns a routing destination for a specific deck and stem.
  void setRoute(core::DeckId deck, core::StemKind stem, StemRouteDestination destination) noexcept;

  /// Convenience method to assign a stem directly to a mixer channel.
  void setChannelRoute(core::DeckId deck, core::StemKind stem, int mixerChannel) noexcept;

  /// Queries current destination for a deck and stem.
  [[nodiscard]] StemRouteDestination route(core::DeckId deck, core::StemKind stem) const noexcept;

  /// Applies standard preset routing: Deck A to Channel 0, Deck B to Channel 1.
  void applyDefaultRouting() noexcept;

  /// Applies split stem preset: Deck A drums+bass to Ch 0, Deck A vocals to Ch 1,
  /// Deck B vocals to Ch 2, Deck B drums+bass+other to Ch 3 (SPEC section 44).
  void applySplitPreset() noexcept;

  /// Routes stem audio from decks into mixer channel buffers.
  /// Zero allocations, lock-free, realtime safe.
  ///
  /// Arguments:
  ///  - deckStemLefts: Array of planar left pointers for each deck [deck][stem]
  ///  - deckStemRights: Array of planar right pointers for each deck [deck][stem]
  ///  - channelLefts: Output array of planar left pointers for mixer channels [ch]
  ///  - channelRights: Output array of planar right pointers for mixer channels [ch]
  ///  - numChannels: Number of output mixer channels (up to kMaxMixerChannels)
  ///  - numSamples: Number of audio frames to route
  void routeStems(const float* const* const* deckStemLefts, const float* const* const* deckStemRights,
                  float* const* channelLefts, float* const* channelRights, int numChannels, int numSamples) noexcept;

 private:
  struct RouteSlot {
    std::atomic<StemRouteDestination::Type> type{StemRouteDestination::Type::DefaultDeckChannel};
    std::atomic<int> channelIndex{0};
  };

  // Matrix indexed by [DeckId][StemKind]
  std::array<std::array<RouteSlot, core::kStemKindCount>, core::kDeckCount> matrix_{};
};

}  // namespace zyron::audio
