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
  VolumeCrossfade       // Smooth progressive fader crossfade
};

[[nodiscard]] constexpr std::string_view transitionStyleName(TransitionStyle s) noexcept {
  switch (s) {
    case TransitionStyle::BassSwap: return "Bass Swap";
    case TransitionStyle::StemBlend: return "Stem Blend";
    case TransitionStyle::QuickCut: return "Quick Cut";
    case TransitionStyle::FilterFade: return "Filter Fade";
    case TransitionStyle::VolumeCrossfade: return "Volume Crossfade";
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
