// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/SetBuilder/SetBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "AI/Recommendation/CompatibilityScorer.hpp"

namespace zyron::ai {

namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

std::vector<float> SetBuilder::generateTargetCurve(
    core::EnergyCurvePreset preset,
    double durationMinutes,
    double stepMinutes) const {
  if (durationMinutes <= 0.0 || stepMinutes <= 0.0) {
    return {5.0f};
  }

  const std::size_t numSteps = static_cast<std::size_t>(std::ceil(durationMinutes / stepMinutes));
  std::vector<float> curve(numSteps, 5.0f);

  for (std::size_t i = 0; i < numSteps; ++i) {
    const float p = (numSteps > 1) ? (static_cast<float>(i) / static_cast<float>(numSteps - 1)) : 0.0f;
    float energy = 5.0f;

    switch (preset) {
      case core::EnergyCurvePreset::PeakHour: {
        // Starts at 6.0, climbs to peak 1 (8.8) at 35%, dips to 6.8 at 55%, climax (9.5) at 85%, cooldown
        if (p < 0.35f) {
          const float subP = p / 0.35f;
          energy = 6.0f + 2.8f * std::sin(0.5f * kPi * subP);
        } else if (p < 0.55f) {
          const float subP = (p - 0.35f) / 0.20f;
          energy = 8.8f - 2.0f * std::sin(0.5f * kPi * subP);
        } else if (p < 0.85f) {
          const float subP = (p - 0.55f) / 0.30f;
          energy = 6.8f + 2.7f * std::sin(0.5f * kPi * subP);
        } else {
          const float subP = (p - 0.85f) / 0.15f;
          energy = 9.5f - 2.5f * subP;
        }
        break;
      }

      case core::EnergyCurvePreset::ProgressiveClimb:
        energy = 4.5f + 5.0f * p;
        break;

      case core::EnergyCurvePreset::WavePattern:
        energy = 6.8f + 2.2f * std::sin(2.5f * 2.0f * kPi * p);
        break;

      case core::EnergyCurvePreset::Warmup:
        energy = 3.8f + 2.8f * p;
        break;

      case core::EnergyCurvePreset::HighEnergyBanger:
        energy = 8.2f + 1.2f * std::sin(3.0f * 2.0f * kPi * p);
        break;

      case core::EnergyCurvePreset::Custom:
        energy = 5.0f;
        break;
    }

    curve[i] = std::clamp(energy, 1.0f, 10.0f);
  }

  return curve;
}

core::SetPlan SetBuilder::buildSet(
    const core::SetBuilderRequest& request,
    const std::vector<core::TrackItem>& catalog) const {
  core::SetPlan plan;
  plan.totalDurationMinutes = 0.0;
  plan.averageCompatibility = 0.0f;
  plan.success = false;

  if (catalog.empty() || request.targetDurationMinutes <= 0.0) {
    plan.summary = "Empty music catalog or invalid duration.";
    return plan;
  }

  // 1. Generate target energy curve
  constexpr double kStepMinutes = 0.5;
  if (request.preset == core::EnergyCurvePreset::Custom && !request.customEnergyCurve.empty()) {
    plan.targetEnergyCurve = request.customEnergyCurve;
  } else {
    plan.targetEnergyCurve = generateTargetCurve(request.preset, request.targetDurationMinutes, kStepMinutes);
  }

  // 2. Filter candidate tracks by genre and BPM window
  std::vector<core::TrackItem> candidates;
  for (const auto& item : catalog) {
    if (item.durationSec < 30.0) continue;

    if (request.minBpm > 0.0 && request.maxBpm > 0.0) {
      if (item.bpm < (request.minBpm - 8.0) || item.bpm > (request.maxBpm + 8.0)) {
        continue;
      }
    }

    if (!request.targetGenre.empty() && !item.genre.empty()) {
      const float genreAffinity = CompatibilityScorer::evaluateGenre(request.targetGenre, item.genre);
      if (genreAffinity < 0.5f) {
        continue;
      }
    }

    candidates.push_back(item);
  }

  if (candidates.empty()) {
    plan.summary = "No matching tracks found for requested genre and BPM window.";
    return plan;
  }

  // 3. Sequential greedy track selection
  std::unordered_set<std::int64_t> usedIds;
  double currentTimeMinutes = 0.0;
  constexpr double kTransitionOverlapMinutes = 0.5;  // 30s DJ mix blend
  float sumCompatibility = 0.0f;

  while (currentTimeMinutes < request.targetDurationMinutes && usedIds.size() < candidates.size()) {
    // Current target energy
    const std::size_t curveIdx = std::min(
        plan.targetEnergyCurve.size() - 1,
        static_cast<std::size_t>(currentTimeMinutes / kStepMinutes));
    const float targetEnergy = plan.targetEnergyCurve[curveIdx];

    const core::TrackItem* bestCandidate = nullptr;
    float bestScore = -1.0f;
    core::CompatibilityScore bestTransition;

    for (const auto& cand : candidates) {
      if (usedIds.find(cand.id) != usedIds.end()) {
        continue;
      }

      // Energy fit to current target curve
      const float energyDiff = std::abs(static_cast<float>(cand.energy) - targetEnergy);
      const float energyFit = std::clamp(1.0f - energyDiff / 4.0f, 0.0f, 1.0f);

      float candidateScore = energyFit;
      core::CompatibilityScore transition;

      if (!plan.tracks.empty()) {
        const auto& prevTrack = plan.tracks.back().track;
        const auto goal = (targetEnergy >= plan.tracks.back().actualEnergy)
                              ? core::EnergyGoal::BuildUp
                              : core::EnergyGoal::CoolDown;
        transition = CompatibilityScorer::score(prevTrack, cand, goal);
        candidateScore = 0.60f * transition.overallScore + 0.40f * energyFit;
      } else {
        transition.overallScore = 1.0f;
        transition.explanation = "Opening track";
      }

      if (candidateScore > bestScore) {
        bestScore = candidateScore;
        bestCandidate = &cand;
        bestTransition = transition;
      }
    }

    if (!bestCandidate) {
      break;
    }

    usedIds.insert(bestCandidate->id);

    core::SetTrackEntry entry;
    entry.track = *bestCandidate;
    entry.trackIndex = static_cast<int>(plan.tracks.size() + 1);
    entry.startTimeMinutes = currentTimeMinutes;
    entry.durationMinutes = bestCandidate->durationSec / 60.0;
    entry.targetEnergy = targetEnergy;
    entry.actualEnergy = static_cast<float>(bestCandidate->energy);
    entry.transitionScore = bestTransition;

    sumCompatibility += bestTransition.overallScore;
    plan.tracks.push_back(entry);

    const double trackAdvance = std::max(0.5, entry.durationMinutes - kTransitionOverlapMinutes);
    currentTimeMinutes += trackAdvance;
  }

  plan.totalDurationMinutes = currentTimeMinutes;
  plan.averageCompatibility = plan.tracks.empty() ? 0.0f : (sumCompatibility / static_cast<float>(plan.tracks.size()));

  // 4. Realized energy curve
  plan.realizedEnergyCurve.resize(plan.targetEnergyCurve.size(), 5.0f);
  for (std::size_t i = 0; i < plan.realizedEnergyCurve.size(); ++i) {
    const double tMin = static_cast<double>(i) * kStepMinutes;
    for (const auto& entry : plan.tracks) {
      if (tMin >= entry.startTimeMinutes && tMin < (entry.startTimeMinutes + entry.durationMinutes)) {
        plan.realizedEnergyCurve[i] = entry.actualEnergy;
        break;
      }
    }
  }

  plan.success = !plan.tracks.empty();

  std::ostringstream ss;
  ss << "Planned " << std::fixed << std::setprecision(1) << plan.totalDurationMinutes
     << "-min " << core::energyCurvePresetName(request.preset) << " set ("
     << plan.tracks.size() << " tracks). Average compatibility: "
     << std::setprecision(0) << (plan.averageCompatibility * 100.0f) << "%.";
  plan.summary = ss.str();

  return plan;
}

}  // namespace zyron::ai
