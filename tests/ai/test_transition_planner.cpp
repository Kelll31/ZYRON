// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "AI/Timeline/CommandTimelineScheduler.hpp"
#include "AI/Transition/TransitionPlanner.hpp"
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

TEST_CASE("TransitionPlanner: Rule-based transition generation (P8-02)", "[ai][transition]") {
  ai::TransitionPlanner planner;

  SECTION("BassSwap transition generates correct sequence and timing") {
    core::TransitionRequest req;
    req.outgoingDeck = core::DeckId::A;
    req.incomingDeck = core::DeckId::B;
    req.style = core::TransitionStyle::BassSwap;
    req.startBeat = 128.0;
    req.durationBeats = 64.0;

    const auto plan = planner.planTransition(req);
    CHECK(plan.isValid);
    CHECK(plan.outgoingDeck == core::DeckId::A);
    CHECK(plan.incomingDeck == core::DeckId::B);
    CHECK(plan.steps.size() >= 8);
    CHECK_FALSE(plan.summary.empty());

    // Check key steps
    bool foundIncomingStart = false;
    bool foundBassSwap = false;
    bool foundOutgoingPause = false;

    for (const auto& step : plan.steps) {
      if (step.beatOffset == 0.0 && std::holds_alternative<core::Play>(step.command)) {
        foundIncomingStart = true;
      }
      if (step.beatOffset == 32.0 && std::holds_alternative<core::SetEq>(step.command)) {
        const auto& eq = std::get<core::SetEq>(step.command);
        if (eq.deck == core::DeckId::B && eq.band == core::EqBand::Low && eq.db == 0.0f) {
          foundBassSwap = true;
        }
      }
      if (step.beatOffset == 64.0 && std::holds_alternative<core::Pause>(step.command)) {
        foundOutgoingPause = true;
      }
    }

    CHECK(foundIncomingStart);
    CHECK(foundBassSwap);
    CHECK(foundOutgoingPause);
  }

  SECTION("StemBlend transition controls individual stem mutes") {
    core::TransitionRequest req;
    req.outgoingDeck = core::DeckId::A;
    req.incomingDeck = core::DeckId::B;
    req.style = core::TransitionStyle::StemBlend;
    req.durationBeats = 64.0;

    const auto plan = planner.planTransition(req);
    CHECK(plan.isValid);

    int stemMuteCommands = 0;
    for (const auto& step : plan.steps) {
      if (std::holds_alternative<core::SetStemMute>(step.command)) {
        ++stemMuteCommands;
      }
    }
    CHECK(stemMuteCommands >= 5);
  }

  SECTION("QuickCut generates immediate downbeat cut") {
    core::TransitionRequest req;
    req.outgoingDeck = core::DeckId::A;
    req.incomingDeck = core::DeckId::B;
    req.style = core::TransitionStyle::QuickCut;

    const auto plan = planner.planTransition(req);
    CHECK(plan.isValid);
    CHECK(plan.steps.size() == 3);
    for (const auto& s : plan.steps) {
      CHECK(s.beatOffset == 0.0);
    }
  }

  SECTION("Scheduling transition onto timeline scheduler") {
    Fixture f;
    ai::CommandTimelineScheduler scheduler(f.bus);

    core::TransitionRequest req;
    req.outgoingDeck = core::DeckId::A;
    req.incomingDeck = core::DeckId::B;
    req.style = core::TransitionStyle::BassSwap;
    req.startBeat = 64.0;
    req.durationBeats = 32.0;

    const auto plan = planner.planTransition(req);
    const auto scheduledCount = planner.scheduleTransition(plan, scheduler);

    CHECK(scheduledCount == plan.steps.size());
    CHECK(scheduler.pendingCount() == plan.steps.size());
  }
}
