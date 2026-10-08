// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <vector>

#include "Core/AI/SetBuilderTypes.hpp"

namespace zyron::ai {

/// Autonomous and assisted DJ set sequence planner (SPEC sections 55, 56, ROADMAP P7-03).
class SetBuilder final : public core::ISetBuilder {
 public:
  SetBuilder() = default;
  ~SetBuilder() override = default;

  [[nodiscard]] std::vector<float> generateTargetCurve(
      core::EnergyCurvePreset preset,
      double durationMinutes,
      double stepMinutes = 0.5) const override;

  [[nodiscard]] core::SetPlan buildSet(
      const core::SetBuilderRequest& request,
      const std::vector<core::TrackItem>& catalog) const override;
};

}  // namespace zyron::ai
