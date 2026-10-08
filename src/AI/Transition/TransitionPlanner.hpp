// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Core/AI/TransitionTypes.hpp"
#include "Core/AI/AutonomousDjTypes.hpp"

namespace zyron::ai {

/// Rule-based transition planner (SPEC section 57, ROADMAP P8-02).
class TransitionPlanner final : public core::ITransitionPlanner {
 public:
  TransitionPlanner() = default;
  ~TransitionPlanner() override = default;

  [[nodiscard]] core::TransitionPlan planTransition(
      const core::TransitionRequest& request) const override;

  std::size_t scheduleTransition(
      const core::TransitionPlan& plan,
      core::ICommandTimelineScheduler& scheduler) const override;

  static constexpr double kMaxMixBeats = 32.0;  // 8 bars: long enough to blend, short enough to keep the energy
  static constexpr double kMinDropMixBeats = 16.0;

  /// How the incoming track enters. With a known drop the transition is its build-up: it starts so that the drop lands
  /// on the last beat, where the outgoing track has just gone (never before the mix-in point; shorter when the intro
  /// is short). Without one it starts at the mix-in point.
  struct IncomingCue {
    double startSec{0.0};
    double durationBeats{kMaxMixBeats};
    bool dropAtEnd{false};
  };
  [[nodiscard]] static IncomingCue cueIncoming(const core::TrackMixPoints& points, double bpm,
                                               double maxBeats) noexcept;
};

}  // namespace zyron::ai
