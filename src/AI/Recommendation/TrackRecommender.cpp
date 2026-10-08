// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Recommendation/TrackRecommender.hpp"

#include <algorithm>

namespace zyron::ai {

core::CompatibilityScore TrackRecommender::scoreCompatibility(
    const core::TrackItem& source,
    const core::TrackItem& candidate,
    core::EnergyGoal goal) const {
  return CompatibilityScorer::score(source, candidate, goal);
}

std::vector<core::TrackRecommendation> TrackRecommender::recommendNextTracks(
    const core::TrackItem& currentTrack,
    const std::vector<core::TrackItem>& catalog,
    const core::RecommendationFilter& filter) const {
  std::vector<core::TrackRecommendation> results;
  if (catalog.empty()) {
    return results;
  }

  for (const auto& candidate : catalog) {
    // Never recommend the currently loaded/playing track
    if (candidate.id == currentTrack.id && candidate.id != 0) {
      continue;
    }
    if (!currentTrack.filepath.empty() && candidate.filepath == currentTrack.filepath) {
      continue;
    }

    // Genre filter if specified
    if (!filter.targetGenre.empty() && !candidate.genre.empty()) {
      const float genreAffinity = CompatibilityScorer::evaluateGenre(filter.targetGenre, candidate.genre);
      if (genreAffinity < 0.6f) {
        continue;
      }
    }

    const auto score = scoreCompatibility(currentTrack, candidate, filter.energyGoal);

    // Tempo filter
    if (std::abs(score.pitchBendPercent) > filter.maxPitchBendPercent) {
      continue;
    }

    // Harmonic Camelot filter
    if (filter.harmonicOnly && !score.isHarmonic) {
      continue;
    }

    core::TrackRecommendation rec;
    rec.track = candidate;
    rec.compatibility = score;
    results.push_back(std::move(rec));
  }

  // Rank by overall compatibility descending
  std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
    return a.compatibility.overallScore > b.compatibility.overallScore;
  });

  // Assign ranks and truncate
  for (std::size_t i = 0; i < results.size(); ++i) {
    results[i].rank = static_cast<int>(i + 1);
  }

  if (filter.maxRecommendations > 0 && results.size() > filter.maxRecommendations) {
    results.resize(filter.maxRecommendations);
  }

  return results;
}

}  // namespace zyron::ai
