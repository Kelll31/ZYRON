// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "AI/Safety/AiSafetyController.hpp"
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

TEST_CASE("AiSafetyController: Emergency kill switch (SPEC section 76, P8-05)", "[ai][safety]") {
  Fixture f;
  core::AiSafetyConfig cfg;
  ai::AiSafetyController safety(f.bus, cfg);

  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "auto_dj"};

  SECTION("Kill switch blocks command dispatch and audit log records event") {
    CHECK_FALSE(safety.isKillSwitchEngaged());

    safety.triggerKillSwitch("Human DJ requested manual takeover");
    CHECK(safety.isKillSwitchEngaged());
    CHECK(safety.killSwitchReason().find("manual takeover") != std::string::npos);

    // Any AI action should now be blocked
    const bool executed = safety.evaluateAndSubmit(core::Play{core::DeckId::A}, origin, 1.0);
    CHECK_FALSE(executed);

    const auto log = safety.getAuditLog();
    REQUIRE(log.size() >= 2);  // kill switch event + blocked attempt
    CHECK(log.back().killSwitchBlocked);
    CHECK_FALSE(log.back().submitted);

    // Resetting restores operation
    safety.resetKillSwitch();
    CHECK_FALSE(safety.isKillSwitchEngaged());

    // Prepare track on Deck A
    (void)f.bus.submit(core::LoadTrack{core::DeckId::A, core::TrackId{1}}, core::CommandOrigin{core::CommandOrigin::Kind::Ui});
    const bool resumed = safety.evaluateAndSubmit(core::Play{core::DeckId::A}, origin, 2.0);
    CHECK(resumed);
  }
}

TEST_CASE("AiSafetyController: Command bus rate limiting (SPEC section 76, P8-05)", "[ai][safety]") {
  Fixture f;
  core::AiSafetyConfig cfg;
  cfg.maxCommandsPerSecond = 5;  // Limit to 5 commands/sec
  cfg.rateLimitEnabled = true;

  ai::AiSafetyController safety(f.bus, cfg);
  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "rate_test"};

  (void)f.bus.submit(core::LoadTrack{core::DeckId::A, core::TrackId{1}}, core::CommandOrigin{core::CommandOrigin::Kind::Ui});

  SECTION("Accepts up to threshold, blocks flood excess, clears on next time window") {
    // Submit 5 commands at t = 1.0
    for (int i = 0; i < 5; ++i) {
      CHECK(safety.evaluateAndSubmit(core::SetVolume{core::DeckId::A, 0.5f}, origin, 1.0));
    }

    // 6th command in same second is blocked
    CHECK_FALSE(safety.evaluateAndSubmit(core::SetVolume{core::DeckId::A, 0.6f}, origin, 1.0));

    const auto log = safety.getAuditLog();
    CHECK(log.back().rateLimited);

    // At t = 2.5 (more than 1.0s later), rate limiter window is clear
    CHECK(safety.evaluateAndSubmit(core::SetVolume{core::DeckId::A, 0.7f}, origin, 2.5));
  }
}

TEST_CASE("AiSafetyController: Dry-run mode and audit logging (P8-05)", "[ai][safety]") {
  Fixture f;
  core::AiSafetyConfig cfg;
  cfg.dryRunMode = true;

  ai::AiSafetyController safety(f.bus, cfg);
  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "dry_test"};

  SECTION("Dry-run reports success but does not alter application state") {
    CHECK(safety.isDryRunMode());

    // In dry-run mode, evaluate returns true
    CHECK(safety.evaluateAndSubmit(core::SetVolume{core::DeckId::A, 0.3f}, origin, 1.0));

    // Application state was NOT altered
    CHECK(f.store.snapshot()->decks[core::index(core::DeckId::A)].volume == 1.0f);

    const auto log = safety.getAuditLog();
    REQUIRE_FALSE(log.empty());
    CHECK(log.back().dryRun);
    CHECK_FALSE(log.back().submitted);

    safety.clearAuditLog();
    CHECK(safety.getAuditLog().empty());
  }
}
