// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Core/AI/RecommendationTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"

namespace zyron::core {

/// Predefined set energy curve profiles (SPEC sections 55, 56, ROADMAP P7-03).
enum class EnergyCurvePreset : std::uint8_t {
  PeakHour = 0,        // Starts medium (6.0), double climax (9.0+), tension breakdown, epic finish
  ProgressiveClimb,    // Continuous gradual build (4.5 -> 9.5)
  WavePattern,         // Alternating peaks and valleys of tension and release
  Warmup,              // Low-to-mid groove (4.0 -> 7.0)
  HighEnergyBanger,    // Non-stop high octane energy (8.0 -> 9.5)
  Custom               // User-provided curve
};

[[nodiscard]] constexpr std::string_view energyCurvePresetName(EnergyCurvePreset p) noexcept {
  switch (p) {
    case EnergyCurvePreset::PeakHour: return "Peak Hour";
    case EnergyCurvePreset::ProgressiveClimb: return "Progressive Climb";
    case EnergyCurvePreset::WavePattern: return "Wave Pattern";
    case EnergyCurvePreset::Warmup: return "Warmup";
    case EnergyCurvePreset::HighEnergyBanger: return "High-Energy Banger";
    case EnergyCurvePreset::Custom: return "Custom";
  }
  return "Unknown";
}

/// Request parameters for autonomous or assisted set building.
struct SetBuilderRequest {
  double targetDurationMinutes{60.0};              // Default 60-minute set (§55)
  std::string targetGenre;                         // e.g. "Drum & Bass", "House"
  double minBpm{160.0};
  double maxBpm{180.0};
  EnergyCurvePreset preset{EnergyCurvePreset::PeakHour};
  std::vector<float> customEnergyCurve;            // Points in range 1.0 .. 10.0 if Custom preset
};

/// An ordered track entry in the generated DJ set.
struct SetTrackEntry {
  TrackItem track;
  double startTimeMinutes{0.0};
  double durationMinutes{0.0};
  float targetEnergy{5.0f};
  float actualEnergy{5.0f};
  CompatibilityScore transitionScore;              // Transition quality from previous track
  int trackIndex{1};                               // 1-indexed position in set
};

/// Complete planned DJ set plan (SPEC sections 55, 56).
struct SetPlan {
  std::vector<SetTrackEntry> tracks;
  double totalDurationMinutes{0.0};
  float averageCompatibility{0.0f};                // Average 0.0 .. 1.0 transition score
  std::vector<float> targetEnergyCurve;            // Target curve sampled every 0.5 min
  std::vector<float> realizedEnergyCurve;          // Actual curve realized by selected tracks
  bool success{false};
  std::string summary;
};

/// Abstract interface for autonomous set building.
class ISetBuilder {
 public:
  virtual ~ISetBuilder() = default;

  /// Generates target energy points for a given preset and duration.
  [[nodiscard]] virtual std::vector<float> generateTargetCurve(
      EnergyCurvePreset preset, double durationMinutes, double stepMinutes = 0.5) const = 0;

  /// Constructs a complete ordered track sequence matching the energy curve and duration.
  [[nodiscard]] virtual SetPlan buildSet(
      const SetBuilderRequest& request,
      const std::vector<TrackItem>& catalog) const = 0;
};

}  // namespace zyron::core
