// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "AI/Recommendation/SemanticSearch.hpp"
#include "Core/AI/SemanticSearchTypes.hpp"
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
    const std::string& genre = "Drum & Bass") {
  core::TrackItem item;
  item.id = id;
  item.title = title;
  item.artist = artist;
  item.album = "Single";
  item.bpm = bpm;
  item.key = key;
  item.energy = energy;
  item.genre = genre;
  item.durationSec = 200.0;
  item.filepath = "/music/" + title + ".mp3";
  return item;
}

}  // namespace

TEST_CASE("SemanticSearch: Query parsing (P7-04)", "[ai][semantic_search]") {
  ai::SemanticSearch searcher;

  SECTION("Parses BPM, genre, and high energy from natural text") {
    const auto parsed = searcher.parseQuery("heavy neurofunk around 174 bpm");
    CHECK(parsed.hasBpmConstraint);
    CHECK(parsed.targetBpm == 174.0);
    CHECK(parsed.hasGenreConstraint);
    CHECK(parsed.detectedGenre == "Neurofunk");
    CHECK(parsed.hasEnergyConstraint);
    CHECK(parsed.minEnergy >= 7.5);
  }

  SECTION("Parses key and chill energy") {
    const auto parsed = searcher.parseQuery("chill liquid in 8a");
    CHECK(parsed.hasKeyConstraint);
    CHECK(parsed.targetKey == "8A");
    CHECK(parsed.hasGenreConstraint);
    CHECK(parsed.detectedGenre == "Liquid");
    CHECK(parsed.hasEnergyConstraint);
    CHECK(parsed.maxEnergy <= 5.5);
  }

  SECTION("Parses techno and exact tempo") {
    const auto parsed = searcher.parseQuery("dark techno 132 bpm");
    CHECK(parsed.hasBpmConstraint);
    CHECK(parsed.targetBpm == 132.0);
    CHECK(parsed.hasGenreConstraint);
    CHECK(parsed.detectedGenre == "Techno");
  }
}

TEST_CASE("SemanticSearch: Search ranking across catalog (P7-04)", "[ai][semantic_search]") {
  ai::SemanticSearch searcher;

  std::vector<core::TrackItem> catalog = {
      makeTrack(1, "Dead Limit", "Noisia", 174.0, "8A", 9.2, "Neurofunk"),
      makeTrack(2, "Twilight's Glow", "Hybrid Minds", 174.0, "8A", 4.2, "Liquid"),
      makeTrack(3, "Solar System", "Sub Focus", 174.0, "9A", 8.8, "Drum & Bass"),
      makeTrack(4, "Your Mind", "Adam Beyer", 128.0, "11B", 7.8, "Techno"),
      makeTrack(5, "Losing It", "Fisher", 126.0, "6A", 7.5, "House")
  };

  SECTION("Searching for 'heavy neurofunk 174 bpm' ranks Noisia #1") {
    const auto results = searcher.search("heavy neurofunk 174 bpm", catalog, 10);
    REQUIRE_FALSE(results.empty());
    CHECK(results.front().track.id == 1);
    CHECK(results.front().matchScore >= 0.85f);
    CHECK_FALSE(results.front().matchReason.empty());
  }

  SECTION("Searching for 'liquid chill' ranks Hybrid Minds #1") {
    const auto results = searcher.search("chill liquid", catalog, 10);
    REQUIRE_FALSE(results.empty());
    CHECK(results.front().track.id == 2);
    CHECK(results.front().matchScore >= 0.70f);
  }

  SECTION("Searching by artist keyword") {
    const auto results = searcher.search("sub focus", catalog, 10);
    REQUIRE_FALSE(results.empty());
    CHECK(results.front().track.id == 3);
  }

  SECTION("Limit restricts maximum returned results") {
    const auto results = searcher.search("174 bpm", catalog, 2);
    CHECK(results.size() <= 2);
  }

  SECTION("Empty inputs return empty results") {
    CHECK(searcher.search("", catalog).empty());
    CHECK(searcher.search("test", {}).empty());
  }
}
