// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

#include "AI/SetBuilder/SetBuilder.hpp"
#include "Core/AI/SetBuilderTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"

using namespace zyron;

namespace {

core::TrackItem makeTrack(
    std::int64_t id,
    const std::string& title,
    double bpm,
    const std::string& key,
    double energy,
    double durationSec = 180.0,
    const std::string& genre = "Drum & Bass") {
  core::TrackItem item;
  item.id = id;
  item.title = title;
  item.artist = "Artist " + std::to_string(id);
  item.bpm = bpm;
  item.key = key;
  item.energy = energy;
  item.genre = genre;
  item.durationSec = durationSec;
  item.filepath = "/music/" + title + ".mp3";
  return item;
}

}  // namespace

TEST_CASE("SetBuilder: Target energy curve generation (P7-03)", "[ai][set_builder]") {
  ai::SetBuilder builder;

  SECTION("Valid duration returns curve sampled at 0.5 min step") {
    const auto curve = builder.generateTargetCurve(core::EnergyCurvePreset::PeakHour, 30.0, 0.5);
    CHECK(curve.size() == 60);

    for (float val : curve) {
      CHECK(val >= 1.0f);
      CHECK(val <= 10.0f);
    }
  }

  SECTION("ProgressiveClimb starts lower and ends higher") {
    const auto curve = builder.generateTargetCurve(core::EnergyCurvePreset::ProgressiveClimb, 30.0, 0.5);
    REQUIRE(curve.size() == 60);
    CHECK(curve.front() < 5.0f);
    CHECK(curve.back() > 9.0f);
    // Non-decreasing
    CHECK(curve.back() > curve.front());
  }

  SECTION("Warmup remains below peak energy") {
    const auto curve = builder.generateTargetCurve(core::EnergyCurvePreset::Warmup, 30.0, 0.5);
    for (float val : curve) {
      CHECK(val <= 7.0f);
    }
  }

  SECTION("HighEnergyBanger sustains high energy throughout") {
    const auto curve = builder.generateTargetCurve(core::EnergyCurvePreset::HighEnergyBanger, 30.0, 0.5);
    for (float val : curve) {
      CHECK(val >= 6.5f);
    }
  }
}

TEST_CASE("SetBuilder: Full set generation from catalog (P7-03)", "[ai][set_builder]") {
  ai::SetBuilder builder;

  // Build a test catalog of 12 Drum & Bass tracks with varying energy and harmonic keys
  std::vector<core::TrackItem> catalog;
  catalog.push_back(makeTrack(1, "Intro Vibe", 174.0, "8A", 5.0, 240.0));
  catalog.push_back(makeTrack(2, "Building Pressure", 174.0, "8B", 6.2, 210.0));
  catalog.push_back(makeTrack(3, "Rolling Bass", 175.0, "9B", 7.0, 180.0));
  catalog.push_back(makeTrack(4, "First Climax", 174.0, "9A", 8.8, 240.0));
  catalog.push_back(makeTrack(5, "Tension Breaker", 173.0, "8A", 6.8, 200.0));
  catalog.push_back(makeTrack(6, "Second Wave", 174.0, "7A", 7.9, 190.0));
  catalog.push_back(makeTrack(7, "Main Event", 175.0, "8A", 9.5, 240.0));
  catalog.push_back(makeTrack(8, "Cooldown Finale", 174.0, "8B", 6.5, 210.0));
  catalog.push_back(makeTrack(9, "Out of Genre Techno", 130.0, "8A", 8.0, 210.0, "Techno"));

  SECTION("Empty catalog returns failure") {
    core::SetBuilderRequest req;
    req.targetDurationMinutes = 15.0;
    const auto plan = builder.buildSet(req, {});
    CHECK_FALSE(plan.success);
    CHECK(plan.tracks.empty());
  }

  SECTION("Builds a 15-minute set matching energy curve") {
    core::SetBuilderRequest req;
    req.targetDurationMinutes = 15.0;
    req.targetGenre = "Drum & Bass";
    req.minBpm = 170.0;
    req.maxBpm = 178.0;
    req.preset = core::EnergyCurvePreset::PeakHour;

    const auto plan = builder.buildSet(req, catalog);
    CHECK(plan.success);
    CHECK(plan.tracks.size() >= 3);
    CHECK(plan.totalDurationMinutes >= 7.0);  // Generated multiple tracks
    CHECK(plan.averageCompatibility > 0.50f);
    CHECK_FALSE(plan.summary.empty());
    CHECK_FALSE(plan.targetEnergyCurve.empty());
    CHECK_FALSE(plan.realizedEnergyCurve.empty());

    // Check track ordering and uniqueness
    std::unordered_set<std::int64_t> seenIds;
    int expectedIndex = 1;
    double lastStartTime = -1.0;

    for (const auto& entry : plan.tracks) {
      CHECK(entry.trackIndex == expectedIndex++);
      CHECK(entry.startTimeMinutes >= lastStartTime);
      lastStartTime = entry.startTimeMinutes;

      // No duplicate tracks
      CHECK(seenIds.find(entry.track.id) == seenIds.end());
      seenIds.insert(entry.track.id);

      // Genre and BPM respected
      CHECK(entry.track.genre == "Drum & Bass");
      CHECK(entry.track.bpm >= 170.0);
      CHECK(entry.track.bpm <= 178.0);
    }
  }
}
