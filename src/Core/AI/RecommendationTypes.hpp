// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/Library/LibraryTypes.hpp"

namespace zyron::core {

/// Direction or intent for track energy transition.
enum class EnergyGoal : std::uint8_t {
  Maintain = 0,  // Keep energy roughly identical (±0.8)
  BuildUp,       // Increase energy (+1.0 .. +2.5) for climbing dancefloor momentum
  CoolDown,      // Decrease energy (-1.0 .. -2.5) for breakdown / breathing room
  Any            // No constraint on energy trajectory
};

/// Fine-grained compatibility assessment between two tracks (SPEC section 52, ROADMAP P7-01).
struct CompatibilityScore {
  float overallScore{0.0f};      // Composite rating: 0.0 .. 1.0 (or 0% .. 100%)
  float bpmScore{0.0f};          // Tempo match compatibility: 0.0 .. 1.0
  float keyScore{0.0f};          // Harmonic Camelot compatibility: 0.0 .. 1.0
  float energyScore{0.0f};       // Energy transition match: 0.0 .. 1.0
  float genreScore{0.0f};        // Genre compatibility factor: 0.0 .. 1.0
  float structureScore{0.0f};    // Phrase & intro/outro duration match: 0.0 .. 1.0

  float bpmDelta{0.0f};          // candidate BPM - source BPM
  float pitchBendPercent{0.0f};  // Required pitch fader shift in % (e.g. +3.2%)
  std::string keyRelation;       // "Identical", "Relative", "Adjacent (+1)", "Adjacent (-1)", "Energy Boost (+2)", "Dissonant"
  std::string explanation;       // Human-readable rationale for the DJ

  bool isHarmonic{false};        // Camelot compatible (same, relative, or adjacent)
  bool isBpmCompatible{false};    // Within acceptable pitch bend limits
};

/// Filter criteria for querying next-track recommendations.
struct RecommendationFilter {
  float maxPitchBendPercent{8.0f};           // Maximum allowed tempo shift (default ±8%)
  bool harmonicOnly{false};                  // Filter out non-harmonic keys
  std::string targetGenre;                   // Optional genre restriction
  EnergyGoal energyGoal{EnergyGoal::Maintain};
  std::size_t maxRecommendations{10};
};

/// A ranked recommendation for the next track in the mix.
struct TrackRecommendation {
  TrackItem track;
  CompatibilityScore compatibility;
  int rank{1};
};

/// Abstract interface for next-track recommendations (SPEC section 52, ROADMAP P7-01, P7-02).
class ITrackRecommender {
 public:
  virtual ~ITrackRecommender() = default;

  /// Scores the compatibility of transitioning from source to candidate track.
  [[nodiscard]] virtual CompatibilityScore scoreCompatibility(
      const TrackItem& source,
      const TrackItem& candidate,
      EnergyGoal goal = EnergyGoal::Maintain) const = 0;

  /// Generates a ranked list of next-track recommendations from a music library catalog.
  [[nodiscard]] virtual std::vector<TrackRecommendation> recommendNextTracks(
      const TrackItem& currentTrack,
      const std::vector<TrackItem>& catalog,
      const RecommendationFilter& filter = {}) const = 0;
};

}  // namespace zyron::core
