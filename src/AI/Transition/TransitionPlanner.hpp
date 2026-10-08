// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Core/AI/TransitionTypes.hpp"

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
};

}  // namespace zyron::ai
