// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <vector>

#include "Core/AI/RecommendationTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"

namespace zyron::ai {

/// Harmonic and rhythmic track transition compatibility engine (SPEC section 52, ROADMAP P7-01).
class CompatibilityScorer {
 public:
  CompatibilityScorer() = default;
  ~CompatibilityScorer() = default;

  /// Evaluates the transition compatibility between source and candidate tracks.
  [[nodiscard]] static core::CompatibilityScore score(
      const core::TrackItem& source,
      const core::TrackItem& candidate,
      core::EnergyGoal goal = core::EnergyGoal::Maintain);

  /// Computes tempo compatibility score and required pitch bend percentage.
  [[nodiscard]] static std::pair<float, float> evaluateBpm(double sourceBpm, double candidateBpm) noexcept;

  /// Computes Camelot wheel harmonic compatibility score and relation description.
  [[nodiscard]] static std::pair<float, std::string> evaluateKey(
      const std::string& sourceKey, const std::string& candidateKey) noexcept;

  /// Computes energy transition score based on target energy goal.
  [[nodiscard]] static float evaluateEnergy(
      double sourceEnergy, double candidateEnergy, core::EnergyGoal goal) noexcept;

  /// Evaluates genre affinity score between two tracks.
  [[nodiscard]] static float evaluateGenre(
      const std::string& sourceGenre, const std::string& candidateGenre) noexcept;
};

}  // namespace zyron::ai
