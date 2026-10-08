// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "AI/Autonomous/AutonomousDjLoop.hpp"
#include "AI/SetBuilder/SetBuilder.hpp"
#include "AI/Timeline/CommandTimelineScheduler.hpp"
#include "AI/Transition/TransitionPlanner.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/AppState.hpp"

using namespace zyron;

namespace {

struct Fixture {
  core::StateStore store;
  core::EventBus events;
  core::CommandBus bus{store, events};
  ai::SetBuilder setBuilder;
  ai::TransitionPlanner transitionPlanner;
  ai::CommandTimelineScheduler scheduler{bus};
  ai::AutonomousDjLoop loop{bus, setBuilder, transitionPlanner, scheduler};
};

core::TrackItem makeTrack(
    std::int64_t id,
    const std::string& title,
    double bpm = 174.0,
    const std::string& key = "8A",
    double energy = 8.0,
    double durationSec = 180.0) {
  core::TrackItem t;
  t.id = id;
  t.title = title;
  t.artist = "Sub Focus";
  t.bpm = bpm;
  t.key = key;
  t.energy = energy;
  t.durationSec = durationSec;
  t.genre = "Drum & Bass";
  t.filepath = "/music/" + title + ".mp3";
  return t;
}

}  // namespace

TEST_CASE("AutonomousDjLoop: Set initialization and lifecycle (P8-03)", "[ai][autonomous_dj]") {
  Fixture f;

  std::vector<core::TrackItem> catalog = {
      makeTrack(1, "Track 1", 174.0, "8A", 6.5, 200.0),
      makeTrack(2, "Track 2", 174.0, "8B", 7.8, 200.0),
      makeTrack(3, "Track 3", 175.0, "9B", 9.0, 200.0)
  };

  core::AutonomousDjConfig config;
  config.targetGenre = "Drum & Bass";
  config.targetDurationMinutes = 10.0;
  config.leadTimeSeconds = 30.0;
  config.transitionDurationBeats = 32.0;

  SECTION("Start initializes Deck A and begins playing first track") {
    CHECK(f.loop.start(config, catalog));

    const auto tel = f.loop.telemetry();
    CHECK(tel.status == core::AutonomousDjStatus::Running);
    CHECK(tel.phase == core::AutonomousDjPhase::PlayingTrack);
    CHECK(tel.activeDeck == core::DeckId::A);
    CHECK(tel.nextDeck == core::DeckId::B);
    CHECK(tel.currentTrackId == 1);
    CHECK(tel.currentTrackIndex == 1);
    CHECK(tel.totalTracksInSet >= 2);
    CHECK_FALSE(tel.statusMessage.empty());

    // StateStore reflects Deck A playing
    const auto state = f.store.snapshot();
    CHECK(state->decks[core::index(core::DeckId::A)].playing);
  }

  SECTION("Playback updates and mix transition triggering") {
    REQUIRE(f.loop.start(config, catalog));

    // Mid-track: remaining time = 100s (> leadTime 30s) -> stays in PlayingTrack
    f.loop.update(60.0, 100.0, 128.0);
    CHECK(f.loop.telemetry().phase == core::AutonomousDjPhase::PlayingTrack);

    // Reaching lead time: remaining time = 25s (<= leadTime 30s) -> triggers transition
    const auto expectedNextId = f.loop.currentSetPlan().tracks[1].track.id;
    f.loop.update(120.0, 25.0, 256.0);
    CHECK(f.loop.telemetry().phase == core::AutonomousDjPhase::ExecutingTransition);
    CHECK(f.loop.telemetry().nextTrackId == expectedNextId);

    // After transition duration completes: decks swap, next track becomes active on Deck B
    f.loop.update(150.0, 0.5, 256.0 + 33.0);
    const auto telSwapped = f.loop.telemetry();
    CHECK(telSwapped.phase == core::AutonomousDjPhase::PlayingTrack);
    CHECK(telSwapped.activeDeck == core::DeckId::B);
    CHECK(telSwapped.nextDeck == core::DeckId::A);
    CHECK(telSwapped.currentTrackId == expectedNextId);
    CHECK(telSwapped.currentTrackIndex == 2);
  }

  SECTION("Pause, resume, and stop controls") {
    REQUIRE(f.loop.start(config, catalog));

    f.loop.pause();
    CHECK(f.loop.telemetry().status == core::AutonomousDjStatus::Paused);

    f.loop.resume();
    CHECK(f.loop.telemetry().status == core::AutonomousDjStatus::Running);

    f.loop.stop();
    CHECK(f.loop.telemetry().status == core::AutonomousDjStatus::Stopped);
    CHECK(f.loop.telemetry().phase == core::AutonomousDjPhase::Idle);
  }
}
