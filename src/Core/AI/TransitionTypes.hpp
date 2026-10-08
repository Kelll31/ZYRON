// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Core/AI/TimelineTypes.hpp"
#include "Core/Commands/Command.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Predefined DJ transition styles (SPEC section 57, ROADMAP P8-02).
enum class TransitionStyle : std::uint8_t {
  BassSwap = 0,         // Swap low EQ bands at breakdown / drop boundary
  StemBlend,            // Fade outgoing bass/vocals while keeping drums, blend incoming stems
  QuickCut,             // Fast 1-bar drop cut
  FilterFade,           // High-pass outgoing, low-pass/open incoming
  VolumeCrossfade,      // Smooth progressive fader crossfade
  LoopRoll,             // Outgoing loops its last bar and rolls it shorter (1, 1/2, 1/4...) into the incoming drop
  Brake,                // Outgoing stops like a turntable brake right before the incoming drop
  Scratch,              // Outgoing is scratched out (baby, transformer, backspin) into the incoming drop
  BeatLoopIn,           // Incoming plays a looped bar of its drop under the outgoing, rolls it shorter, then drops
  DoubleDrop,           // Both drops at once (compatible keys): bass swapped on the drop, the outgoing fades later
  EchoOut,              // The outgoing track leaves in a beat-synced echo that rings on after its fader is down
  ReverbOut             // The same with a reverb wash
};

[[nodiscard]] constexpr std::string_view transitionStyleName(TransitionStyle s) noexcept {
  switch (s) {
    case TransitionStyle::BassSwap: return "Bass Swap";
    case TransitionStyle::StemBlend: return "Stem Blend";
    case TransitionStyle::QuickCut: return "Quick Cut";
    case TransitionStyle::FilterFade: return "Filter Fade";
    case TransitionStyle::VolumeCrossfade: return "Volume Crossfade";
    case TransitionStyle::LoopRoll: return "Loop Roll";
    case TransitionStyle::Brake: return "Brake";
    case TransitionStyle::Scratch: return "Scratch";
    case TransitionStyle::BeatLoopIn: return "Beat Loop";
    case TransitionStyle::DoubleDrop: return "Double Drop";
    case TransitionStyle::EchoOut: return "Echo Out";
    case TransitionStyle::ReverbOut: return "Reverb Out";
  }
  return "Unknown";
}

/// Request parameters to construct an automated DJ transition.
struct TransitionRequest {
  DeckId outgoingDeck{DeckId::A};
  DeckId incomingDeck{DeckId::B};
  TransitionStyle style{TransitionStyle::BassSwap};
  double startBeat{0.0};              // Absolute master beat where the transition starts
  double durationBeats{64.0};         // Transition span in beats (default 16 bars = 64 beats)
  double outgoingBpm{174.0};
  double incomingBpm{174.0};
  bool enableSync{true};
  bool dropAtEnd{false};  // the incoming drop lands on the last beat: the outgoing track must be gone by then
  double outgoingStartSec{-1.0};    // outgoing track position at startBeat (seconds); needed for loop rolls
  double outgoingSpeed{1.0};        // the outgoing deck's playback speed (a brake ramps it down and restores it)
  double outgoingLoopStartSec{-1.0};  // > 0: loop the outgoing here from the start (its next drop never arrives)
  double outgoingLoopBeats{0.0};
  double incomingDropSec{-1.0};  // the incoming drop (track seconds): the beat a BeatLoopIn loops
  bool fxHits{false};  // add impacts / air horns / risers on the drop (battle mode)
  bool holdIncomingMids{false};  // both tracks have vocals in the window: the incoming mids stay down until the swap
  int variation{0};  // picks one of several routines (scratches, rolls) so the same style never repeats exactly
};

/// A single step in an automated transition plan.
struct TransitionStep {
  double beatOffset{0.0};             // Relative offset from transition start beat
  Command command{Play{DeckId::A}};
  std::string description;
};

/// Complete planned transition containing sequential automated steps.
struct TransitionPlan {
  DeckId outgoingDeck{DeckId::A};
  DeckId incomingDeck{DeckId::B};
  TransitionStyle style{TransitionStyle::BassSwap};
  double startBeat{0.0};
  double durationBeats{64.0};
  std::vector<TransitionStep> steps;
  bool isValid{false};
  std::string summary;
};

/// Abstract interface for transition planning (SPEC section 57, ROADMAP P8-02).
class ITransitionPlanner {
 public:
  virtual ~ITransitionPlanner() = default;

  /// Generates a rule-based transition plan based on request parameters.
  [[nodiscard]] virtual TransitionPlan planTransition(const TransitionRequest& request) const = 0;

  /// Submits the plan steps to the command timeline scheduler.
  virtual std::size_t scheduleTransition(
      const TransitionPlan& plan, ICommandTimelineScheduler& scheduler) const = 0;
};

}  // namespace zyron::core
