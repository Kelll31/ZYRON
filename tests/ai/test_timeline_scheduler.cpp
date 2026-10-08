// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "AI/Timeline/CommandTimelineScheduler.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/State/AppState.hpp"

using namespace zyron;

namespace {

struct Fixture {
  core::StateStore store;
  core::EventBus events;
  core::CommandBus bus{store, events};
};

}  // namespace

TEST_CASE("CommandTimelineScheduler: Beat scheduling and quantisation (P8-01)", "[ai][timeline]") {
  Fixture f;
  ai::CommandTimelineScheduler scheduler(f.bus);

  SECTION("Quantisation round-up to grid boundaries") {
    CHECK(ai::CommandTimelineScheduler::quantiseToGrid(3.1, core::QuantiseGrid::Bar) == 4.0);
    CHECK(ai::CommandTimelineScheduler::quantiseToGrid(4.0, core::QuantiseGrid::Bar) == 4.0);
    CHECK(ai::CommandTimelineScheduler::quantiseToGrid(5.0, core::QuantiseGrid::TwoBars) == 8.0);
    CHECK(ai::CommandTimelineScheduler::quantiseToGrid(17.0, core::QuantiseGrid::Phrase8) == 32.0);
    CHECK(ai::CommandTimelineScheduler::quantiseToGrid(2.5, core::QuantiseGrid::None) == 2.5);
  }

  SECTION("Scheduled beat command execution on beat advancement") {
    // Prepare track on Deck A so SetVolume is accepted
    (void)f.bus.submit(core::LoadTrack{core::DeckId::A, core::TrackId{1}}, core::CommandOrigin{core::CommandOrigin::Kind::Ui});

    const auto id = scheduler.scheduleAtBeat(
        core::SetVolume{core::DeckId::A, 0.5f},
        3.2,
        core::QuantiseGrid::Bar,
        "Drop fader to half on bar 1");

    CHECK(id > 0);
    CHECK(scheduler.pendingCount() == 1);
    CHECK(scheduler.executedCount() == 0);

    // Advancing before target beat does not execute
    const auto executedBefore = scheduler.advanceToBeats(core::DeckId::A, 3.9);
    CHECK(executedBefore == 0);
    CHECK(scheduler.pendingCount() == 1);

    // Advancing to target beat 4.0 executes command
    const auto executedAt = scheduler.advanceToBeats(core::DeckId::A, 4.0);
    CHECK(executedAt == 1);
    CHECK(scheduler.pendingCount() == 0);
    CHECK(scheduler.executedCount() == 1);
  }

  SECTION("Time-based command execution") {
    (void)f.bus.submit(core::LoadTrack{core::DeckId::B, core::TrackId{2}}, core::CommandOrigin{core::CommandOrigin::Kind::Ui});

    scheduler.scheduleAtTime(core::SetVolume{core::DeckId::B, 0.7f}, 5.0, "Time-based fade");
    CHECK(scheduler.advanceToTime(4.9) == 0);
    CHECK(scheduler.advanceToTime(5.1) == 1);
    CHECK(scheduler.executedCount() == 1);
  }
}

TEST_CASE("CommandTimelineScheduler: User-override-wins rule (ARCHITECTURE section 5, P8-01)", "[ai][timeline]") {
  Fixture f;
  ai::CommandTimelineScheduler scheduler(f.bus);

  (void)f.bus.submit(core::LoadTrack{core::DeckId::A, core::TrackId{1}}, core::CommandOrigin{core::CommandOrigin::Kind::Ui});
  (void)f.bus.submit(core::LoadTrack{core::DeckId::B, core::TrackId{2}}, core::CommandOrigin{core::CommandOrigin::Kind::Ui});

  // Schedule future AI automation on Deck A and Deck B
  scheduler.scheduleAtBeat(core::SetEq{core::DeckId::A, core::EqBand::Low, -12.0f}, 16.0);
  scheduler.scheduleAtBeat(core::SetEq{core::DeckId::A, core::EqBand::Low, -24.0f}, 32.0);
  scheduler.scheduleAtBeat(core::SetEq{core::DeckId::B, core::EqBand::Low, -12.0f}, 16.0);

  CHECK(scheduler.pendingCount() == 3);

  SECTION("Manual user command on Deck A cancels pending AI actions on Deck A but preserves Deck B") {
    bool callbackTriggered = false;
    scheduler.setOnOverrideCallback([&](const core::TimelineOverrideEvent& evt) {
      if (evt.deck == core::DeckId::A) {
        callbackTriggered = true;
      }
    });

    // Human DJ touches knob on Deck A (UI origin)
    scheduler.handleUserCommand(
        core::SetEq{core::DeckId::A, core::EqBand::Low, 0.0f},
        core::CommandOrigin{core::CommandOrigin::Kind::Ui, "gui_knob"});

    CHECK(callbackTriggered);
    CHECK(scheduler.isDeckOverridden(core::DeckId::A));
    CHECK_FALSE(scheduler.isDeckOverridden(core::DeckId::B));

    // Two AI commands on Deck A should be cancelled; Deck B remains pending
    CHECK(scheduler.pendingCount() == 1);
    CHECK(scheduler.cancelledCount() == 2);

    // Advancing past beat 32: only Deck B's command executes
    const auto executed = scheduler.advanceToBeats(core::DeckId::A, 32.0);
    CHECK(executed == 1);
    CHECK(scheduler.pendingCount() == 0);

    // Override flag can be cleared by DJ/system to restore AI
    scheduler.clearDeckOverride(core::DeckId::A);
    CHECK_FALSE(scheduler.isDeckOverridden(core::DeckId::A));
  }

  SECTION("Cancel command by ID and clear all") {
    CHECK(scheduler.cancelDeckCommands(core::DeckId::A) == 2);
    CHECK(scheduler.pendingCount() == 1);

    scheduler.clear();
    CHECK(scheduler.pendingCount() == 0);
  }
}
