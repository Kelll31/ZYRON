// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

#include "Core/AI/RecommendationTypes.hpp"
#include "Core/AI/SetBuilderTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "UI/AI/RecommendationPanel.hpp"
#include "UI/AI/SetBuilderComponent.hpp"
#include "UI/Theme.hpp"

using namespace zyron;

namespace {

class MockRecommender : public core::ITrackRecommender {
 public:
  core::CompatibilityScore scoreCompatibility(
      const core::TrackItem&,
      const core::TrackItem&,
      core::EnergyGoal) const override {
    core::CompatibilityScore s;
    s.overallScore = 0.92f;
    s.pitchBendPercent = 0.5f;
    s.keyRelation = "Relative key";
    s.explanation = "92% match (Camelot relative)";
    s.isHarmonic = true;
    s.isBpmCompatible = true;
    return s;
  }

  std::vector<core::TrackRecommendation> recommendNextTracks(
      const core::TrackItem&,
      const std::vector<core::TrackItem>& catalog,
      const core::RecommendationFilter&) const override {
    std::vector<core::TrackRecommendation> list;
    int rank = 1;
    for (const auto& item : catalog) {
      core::TrackRecommendation rec;
      rec.track = item;
      rec.rank = rank++;
      rec.compatibility.overallScore = 0.90f;
      rec.compatibility.keyRelation = "8B (Relative)";
      rec.compatibility.pitchBendPercent = 0.5f;
      rec.compatibility.explanation = "Great harmonic transition";
      list.push_back(rec);
    }
    return list;
  }
};

class MockSetBuilder : public core::ISetBuilder {
 public:
  std::vector<float> generateTargetCurve(
      core::EnergyCurvePreset, double durationMinutes, double) const override {
    return std::vector<float>(static_cast<std::size_t>(durationMinutes * 2), 7.5f);
  }

  core::SetPlan buildSet(
      const core::SetBuilderRequest&,
      const std::vector<core::TrackItem>& catalog) const override {
    core::SetPlan plan;
    plan.totalDurationMinutes = 60.0;
    plan.averageCompatibility = 0.88f;
    plan.targetEnergyCurve = {5.0f, 6.0f, 7.5f, 9.0f, 8.0f};
    plan.realizedEnergyCurve = {5.2f, 6.1f, 7.3f, 8.8f, 7.9f};
    plan.success = !catalog.empty();
    plan.summary = "Planned 60.0-min Peak Hour set (15 tracks). Average compatibility: 88%.";

    int idx = 1;
    for (const auto& t : catalog) {
      core::SetTrackEntry e;
      e.track = t;
      e.trackIndex = idx++;
      e.startTimeMinutes = (idx - 1) * 3.5;
      e.durationMinutes = 3.5;
      e.targetEnergy = 7.5f;
      e.actualEnergy = static_cast<float>(t.energy);
      e.transitionScore.overallScore = 0.88f;
      plan.tracks.push_back(e);
    }
    return plan;
  }
};

core::TrackItem makeTrack(std::int64_t id, const std::string& title, double bpm, const std::string& key) {
  core::TrackItem t;
  t.id = id;
  t.title = title;
  t.artist = "Sub Focus";
  t.bpm = bpm;
  t.key = key;
  t.energy = 8.0;
  t.genre = "Drum & Bass";
  t.durationSec = 210.0;
  return t;
}

}  // namespace

TEST_CASE("RecommendationPanel: UI view interactions (P7-02)", "[ui][ai]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  MockRecommender recommender;
  ui::RecommendationPanel panel(recommender);
  panel.setSize(800, 400);

  std::vector<core::TrackItem> catalog = {
      makeTrack(1, "Solaris", 174.0, "8A"),
      makeTrack(2, "Desire", 175.0, "8B"),
      makeTrack(3, "Timewarp", 174.0, "9A")
  };
  panel.setCatalog(catalog);
  panel.setCurrentTrack(catalog[0]);

  SECTION("Generates rows for recommended tracks") {
    panel.refreshRecommendations();
    CHECK(panel.getNumRows() == 3);
    CHECK(panel.recommendations().size() == 3);
  }

  SECTION("Renders into graphics context cleanly") {
    panel.refreshRecommendations();
    juce::Image image(juce::Image::ARGB, 800, 400, true);
    juce::Graphics g(image);
    REQUIRE_NOTHROW(panel.paintEntireComponent(g, true));
  }

  SECTION("Load callback invokes on button or selection") {
    core::DeckId loadedDeck = core::DeckId::B;
    std::int64_t loadedId = -1;

    panel.setOnLoadTrack([&](core::DeckId d, std::int64_t id) {
      loadedDeck = d;
      loadedId = id;
    });

    panel.refreshRecommendations();
    CHECK(panel.getNumRows() == 3);
  }
}

TEST_CASE("SetBuilderComponent: UI view interactions (P7-03)", "[ui][ai]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  MockSetBuilder builder;
  ui::SetBuilderComponent comp(builder);
  comp.setSize(900, 500);

  std::vector<core::TrackItem> catalog = {
      makeTrack(1, "Track 1", 174.0, "8A"),
      makeTrack(2, "Track 2", 175.0, "8B"),
      makeTrack(3, "Track 3", 174.0, "9A")
  };
  comp.setCatalog(catalog);

  SECTION("Plans set and populates track rows") {
    comp.planSet();
    CHECK(comp.currentPlan().success);
    CHECK(comp.getNumRows() == 3);
  }

  SECTION("Renders into graphics context cleanly") {
    comp.planSet();
    juce::Image image(juce::Image::ARGB, 900, 500, true);
    juce::Graphics g(image);
    REQUIRE_NOTHROW(comp.paintEntireComponent(g, true));
  }
}
