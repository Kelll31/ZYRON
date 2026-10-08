// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "AI/Recommendation/TrackRecommender.hpp"
#include "Core/AI/RecommendationTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"

using namespace zyron;

namespace {

core::TrackItem makeTrack(
    std::int64_t id,
    const std::string& title,
    const std::string& artist,
    double bpm,
    const std::string& key,
    double energy,
    const std::string& genre = "Drum & Bass",
    double durationSec = 210.0) {
  core::TrackItem item;
  item.id = id;
  item.title = title;
  item.artist = artist;
  item.bpm = bpm;
  item.key = key;
  item.energy = energy;
  item.genre = genre;
  item.durationSec = durationSec;
  item.filepath = "/music/" + title + ".mp3";
  return item;
}

}  // namespace

TEST_CASE("TrackRecommender: Basic next-track ranking (P7-02)", "[ai][recommendation]") {
  ai::TrackRecommender recommender;

  const auto current = makeTrack(1, "Desire", "Sub Focus", 174.0, "8A", 8.0);

  std::vector<core::TrackItem> catalog = {
      makeTrack(1, "Desire", "Sub Focus", 174.0, "8A", 8.0),          // same track (should be excluded)
      makeTrack(2, "Solaris", "Dimension", 174.0, "8B", 8.2),         // relative key, perfect BPM -> top rank
      makeTrack(3, "Timewarp", "Sub Focus", 175.0, "9A", 8.5),        // adjacent key, small shift -> strong match
      makeTrack(4, "Deep Down", "Alix Perez", 172.0, "3A", 4.0),      // dissonant key, low energy -> low score
      makeTrack(5, "House Anthem", "Fisher", 126.0, "8A", 7.0, "Tech House") // large BPM difference
  };

  SECTION("Excludes currently playing track and sorts by overall compatibility") {
    core::RecommendationFilter filter;
    filter.maxPitchBendPercent = 10.0f;
    filter.harmonicOnly = false;
    filter.maxRecommendations = 10;

    const auto results = recommender.recommendNextTracks(current, catalog, filter);
    REQUIRE_FALSE(results.empty());

    // Current track must be excluded
    for (const auto& rec : results) {
      CHECK(rec.track.id != current.id);
      CHECK(rec.track.filepath != current.filepath);
    }

    // Top recommendation should be Solaris (Dimension)
    CHECK(results.front().track.id == 2);
    CHECK(results.front().rank == 1);
    CHECK(results.front().compatibility.overallScore >= 0.85f);
    CHECK(results.front().compatibility.isHarmonic);
  }

  SECTION("Harmonic-only filter discards non-harmonic tracks") {
    core::RecommendationFilter filter;
    filter.harmonicOnly = true;

    const auto results = recommender.recommendNextTracks(current, catalog, filter);
    for (const auto& rec : results) {
      CHECK(rec.compatibility.isHarmonic);
      CHECK(rec.track.id != 4);  // 3A is dissonant to 8A
    }
  }

  SECTION("Tempo pitch bend limit filter") {
    core::RecommendationFilter filter;
    filter.maxPitchBendPercent = 4.0f;  // ±4% limit

    const auto results = recommender.recommendNextTracks(current, catalog, filter);
    for (const auto& rec : results) {
      CHECK(std::abs(rec.compatibility.pitchBendPercent) <= 4.0f);
      CHECK(rec.track.id != 5);  // 126 BPM is ~-27%
    }
  }

  SECTION("Max recommendations limits output size") {
    core::RecommendationFilter filter;
    filter.maxRecommendations = 2;

    const auto results = recommender.recommendNextTracks(current, catalog, filter);
    CHECK(results.size() == 2);
    CHECK(results[0].rank == 1);
    CHECK(results[1].rank == 2);
  }
}

TEST_CASE("TrackRecommender: Energy trajectories (P7-02)", "[ai][recommendation]") {
  ai::TrackRecommender recommender;
  const auto current = makeTrack(1, "Base Track", "Artist", 174.0, "8A", 5.0);

  std::vector<core::TrackItem> catalog = {
      makeTrack(2, "Peak Energy Track", "Artist", 174.0, "8A", 7.0),
      makeTrack(3, "Subtle Groove Track", "Artist", 174.0, "8A", 5.0),
      makeTrack(4, "Deep Chill Track", "Artist", 174.0, "8A", 3.0),
  };

  SECTION("BuildUp goal favors higher energy") {
    core::RecommendationFilter filter;
    filter.energyGoal = core::EnergyGoal::BuildUp;

    const auto results = recommender.recommendNextTracks(current, catalog, filter);
    REQUIRE(results.size() == 3);
    CHECK(results.front().track.id == 2);  // 7.0 (+2.0) is optimal build-up
  }

  SECTION("CoolDown goal favors lower energy") {
    const auto highEnergyCurrent = makeTrack(10, "High Track", "Artist", 174.0, "8A", 7.0);
    core::RecommendationFilter filter;
    filter.energyGoal = core::EnergyGoal::CoolDown;

    const auto results = recommender.recommendNextTracks(highEnergyCurrent, catalog, filter);
    REQUIRE(results.size() == 3);
    CHECK(results.front().track.id == 3);  // 5.0 (-2.0) is optimal cool-down
    CHECK(results.front().track.energy < 6.0);
  }
}
