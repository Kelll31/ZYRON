// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <vector>

#include "AI/Recommendation/CompatibilityScorer.hpp"
#include "Core/AI/RecommendationTypes.hpp"

namespace zyron::ai {

/// Next-track recommendation service for live DJing and planning (SPEC section 52, ROADMAP P7-02).
class TrackRecommender final : public core::ITrackRecommender {
 public:
  TrackRecommender() = default;
  ~TrackRecommender() override = default;

  [[nodiscard]] core::CompatibilityScore scoreCompatibility(
      const core::TrackItem& source,
      const core::TrackItem& candidate,
      core::EnergyGoal goal = core::EnergyGoal::Maintain) const override;

  [[nodiscard]] std::vector<core::TrackRecommendation> recommendNextTracks(
      const core::TrackItem& currentTrack,
      const std::vector<core::TrackItem>& catalog,
      const core::RecommendationFilter& filter = {}) const override;
};

}  // namespace zyron::ai
