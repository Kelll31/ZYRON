// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "AI/Recommendation/CompatibilityScorer.hpp"
#include "Core/AI/RecommendationTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"

using namespace zyron;

TEST_CASE("CompatibilityScorer: BPM matching and pitch bend calculation (P7-01)", "[ai][recommendation]") {
  SECTION("Exact BPM match") {
    const auto [score, shift] = ai::CompatibilityScorer::evaluateBpm(174.0, 174.0);
    CHECK(score == 1.0f);
    CHECK(std::abs(shift) < 1e-4f);
  }

  SECTION("Small pitch shift within ±2% yields perfect 1.0 score") {
    const auto [score, shift] = ai::CompatibilityScorer::evaluateBpm(174.0, 176.0);
    CHECK(score == 1.0f);
    CHECK(shift > 0.0f);
    CHECK(shift < 2.0f);
  }

  SECTION("Moderate pitch shift within ±6% yields high score >= 0.8") {
    const auto [score, shift] = ai::CompatibilityScorer::evaluateBpm(174.0, 182.0);
    CHECK(score >= 0.8f);
    CHECK(score < 1.0f);
  }

  SECTION("Large pitch shift > 16% yields zero score") {
    const auto [score, shift] = ai::CompatibilityScorer::evaluateBpm(174.0, 130.0);
    CHECK(score == 0.0f);
  }

  SECTION("Half-time and double-time octave resolution (87 BPM vs 174 BPM)") {
    const auto [score, shift] = ai::CompatibilityScorer::evaluateBpm(174.0, 87.0);
    CHECK(score == 1.0f);
    CHECK(std::abs(shift) < 1e-4f);
  }
}

TEST_CASE("CompatibilityScorer: Camelot wheel harmonic relations (P7-01)", "[ai][recommendation]") {
  SECTION("Identical key (8A -> 8A)") {
    const auto [score, rel] = ai::CompatibilityScorer::evaluateKey("8A", "8A");
    CHECK(score == 1.0f);
    CHECK(rel == "Identical key");
  }

  SECTION("Relative key (8A -> 8B)") {
    const auto [score, rel] = ai::CompatibilityScorer::evaluateKey("8A", "8B");
    CHECK(score >= 0.90f);
    CHECK(rel == "Relative key");
  }

  SECTION("Adjacent keys (+1 / -1 hour, same mode: 8A -> 9A, 8A -> 7A)") {
    const auto [scoreUp, relUp] = ai::CompatibilityScorer::evaluateKey("8A", "9A");
    const auto [scoreDown, relDown] = ai::CompatibilityScorer::evaluateKey("8A", "7A");
    CHECK(scoreUp >= 0.80f);
    CHECK(scoreDown >= 0.80f);
    CHECK(relUp.find("Adjacent") != std::string::npos);
  }

  SECTION("Energy boost modulation (+2 semitones: 8A -> 10A)") {
    const auto [score, rel] = ai::CompatibilityScorer::evaluateKey("8A", "10A");
    CHECK(score >= 0.60f);
    CHECK(rel.find("Energy boost") != std::string::npos);
  }

  SECTION("Circular wrap around 12 to 1 (12A -> 1A)") {
    const auto [score, rel] = ai::CompatibilityScorer::evaluateKey("12A", "1A");
    CHECK(score >= 0.80f);
    CHECK(rel.find("Adjacent") != std::string::npos);
  }

  SECTION("Dissonant key clash (8A -> 3A)") {
    const auto [score, rel] = ai::CompatibilityScorer::evaluateKey("8A", "3A");
    CHECK(score <= 0.30f);
    CHECK(rel.find("clash") != std::string::npos);
  }
}

TEST_CASE("CompatibilityScorer: Energy trajectory evaluation (P7-01)", "[ai][recommendation]") {
  SECTION("Maintain goal") {
    CHECK(ai::CompatibilityScorer::evaluateEnergy(7.5, 7.5, core::EnergyGoal::Maintain) == 1.0f);
    CHECK(ai::CompatibilityScorer::evaluateEnergy(7.5, 7.8, core::EnergyGoal::Maintain) >= 0.85f);
    CHECK(ai::CompatibilityScorer::evaluateEnergy(7.5, 3.0, core::EnergyGoal::Maintain) == 0.0f);
  }

  SECTION("BuildUp goal rewards increasing energy") {
    const float rising = ai::CompatibilityScorer::evaluateEnergy(6.0, 8.0, core::EnergyGoal::BuildUp);
    const float falling = ai::CompatibilityScorer::evaluateEnergy(8.0, 5.0, core::EnergyGoal::BuildUp);
    CHECK(rising == 1.0f);
    CHECK(falling < 0.5f);
  }

  SECTION("CoolDown goal rewards decreasing energy") {
    const float falling = ai::CompatibilityScorer::evaluateEnergy(8.5, 6.5, core::EnergyGoal::CoolDown);
    const float rising = ai::CompatibilityScorer::evaluateEnergy(6.0, 8.5, core::EnergyGoal::CoolDown);
    CHECK(falling == 1.0f);
    CHECK(rising < 0.5f);
  }
}

TEST_CASE("CompatibilityScorer: Overall track transition score and explanation (P7-01)", "[ai][recommendation]") {
  core::TrackItem source;
  source.title = "Solaris";
  source.artist = "Sub Focus";
  source.bpm = 174.0;
  source.key = "8A";
  source.energy = 8.0;
  source.genre = "Drum & Bass";
  source.durationSec = 240.0;

  core::TrackItem perfectMatch;
  perfectMatch.title = "Desire";
  perfectMatch.artist = "Dimension";
  perfectMatch.bpm = 175.0;
  perfectMatch.key = "8B";
  perfectMatch.energy = 8.2;
  perfectMatch.genre = "Drum & Bass";
  perfectMatch.durationSec = 220.0;

  const auto score = ai::CompatibilityScorer::score(source, perfectMatch);
  CHECK(score.overallScore >= 0.90f);
  CHECK(score.isHarmonic);
  CHECK(score.isBpmCompatible);
  CHECK_FALSE(score.explanation.empty());
  CHECK(score.explanation.find("Relative key") != std::string::npos);
}
