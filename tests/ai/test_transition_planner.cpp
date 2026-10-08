// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <variant>
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

TEST_CASE("TransitionPlanner: the drop ends the transition; the outgoing track is gone by then", "[ai][transition][drop]") {
  core::TrackMixPoints points;
  points.mixInSec = 0.0;
  points.dropSec = 60.0;  // a long intro
  const double bpm = 174.0;
  const auto cue = ai::TransitionPlanner::cueIncoming(points, bpm, 64.0);
  REQUIRE(cue.dropAtEnd);
  CHECK(cue.durationBeats == ai::TransitionPlanner::kMaxMixBeats);
  CHECK(std::abs(cue.startSec + cue.durationBeats * 60.0 / bpm - points.dropSec) < 1e-9);

  core::TransitionRequest req;
  req.style = core::TransitionStyle::BassSwap;
  req.durationBeats = cue.durationBeats;
  req.dropAtEnd = true;
  const auto plan = ai::TransitionPlanner{}.planTransition(req);
  double outgoingSilentAt = 1e9;
  double incomingBassAt = -1.0;
  for (const auto& step : plan.steps) {
    if (const auto* v = std::get_if<core::SetVolume>(&step.command); v && v->deck == req.outgoingDeck && v->linear == 0.0F) {
      outgoingSilentAt = std::min(outgoingSilentAt, step.beatOffset);
    }
    if (const auto* eq = std::get_if<core::SetEq>(&step.command);
        eq && eq->deck == req.incomingDeck && eq->band == core::EqBand::Low && eq->db == 0.0F) {
      incomingBassAt = step.beatOffset;
    }
  }
  CHECK(outgoingSilentAt < cue.durationBeats);
  CHECK(incomingBassAt == cue.durationBeats);

  SECTION("a short intro shortens the transition instead of starting before the track") {
    core::TrackMixPoints shortIntro;
    shortIntro.mixInSec = 0.0;
    shortIntro.dropSec = 16 * 4 * 60.0 / bpm * 0.5;  // 8 bars before the drop
    const auto c = ai::TransitionPlanner::cueIncoming(shortIntro, bpm, 64.0);
    CHECK(c.dropAtEnd);
    CHECK(c.durationBeats == 32.0);
    CHECK(c.startSec >= 0.0);
  }
}

TEST_CASE("TransitionPlanner: loop roll, brake and filter sweep leave the outgoing deck clean", "[ai][transition][styles]") {
  core::TransitionRequest req;
  req.durationBeats = 32.0;
  req.dropAtEnd = true;
  req.outgoingBpm = 174.0;
  req.outgoingStartSec = 300.0;
  req.outgoingSpeed = 1.02;

  const auto stepsOf = [&](core::TransitionStyle style) {
    req.style = style;
    return ai::TransitionPlanner{}.planTransition(req).steps;
  };

  SECTION("a loop roll halves its loop and switches it off at the end") {
    double lastLength = 1e9;
    int rolls = 0;
    bool loopOff = false;
    for (const auto& step : stepsOf(core::TransitionStyle::LoopRoll)) {
      if (const auto* loop = std::get_if<core::SetLoop>(&step.command); loop && loop->deck == req.outgoingDeck) {
        if (loop->active) {
          const double length = loop->endSeconds - loop->startSeconds;
          CHECK(length < lastLength);
          lastLength = length;
          ++rolls;
        } else {
          loopOff = step.beatOffset == req.durationBeats;
        }
      }
    }
    CHECK(rolls >= 4);
    CHECK(loopOff);
  }

  SECTION("a brake stops the outgoing deck on the beat before the drop and restores its speed once stopped") {
    double brakeAt = -1.0;
    double restoredAt = -1.0;
    for (const auto& step : stepsOf(core::TransitionStyle::Brake)) {
      if (const auto* scratch = std::get_if<core::Scratch>(&step.command);
          scratch && scratch->deck == req.outgoingDeck && scratch->pattern == core::ScratchPattern::Brake) {
        brakeAt = step.beatOffset;
      }
      if (const auto* speed = std::get_if<core::SetPlaybackSpeed>(&step.command);
          speed && speed->deck == req.outgoingDeck && std::abs(speed->speed - req.outgoingSpeed) < 1e-9) {
        restoredAt = step.beatOffset;
      }
    }
    CHECK(brakeAt == req.durationBeats - 1.0);
    CHECK(restoredAt == req.durationBeats);
  }

  SECTION("a filter sweep resets both filters") {
    float inLast = 1.0F;
    float outLast = 1.0F;
    for (const auto& step : stepsOf(core::TransitionStyle::FilterFade)) {
      if (const auto* f = std::get_if<core::SetFilter>(&step.command)) {
        (f->deck == req.incomingDeck ? inLast : outLast) = f->position;
      }
    }
    CHECK(inLast == 0.0F);
    CHECK(outLast == 0.0F);
  }
}

TEST_CASE("TransitionPlanner: beat loop-in and a slow tempo settle", "[ai][transition][beatloop]") {
  core::TransitionRequest req;
  req.style = core::TransitionStyle::BeatLoopIn;
  req.durationBeats = 32.0;
  req.dropAtEnd = true;
  req.outgoingBpm = 172.0;
  req.incomingBpm = 175.0;
  req.incomingDropSec = 90.0;
  const auto plan = ai::TransitionPlanner{}.planTransition(req);

  int incomingLoops = 0;
  bool released = false;
  bool seekToDrop = false;
  double settleTo = -1.0;
  double settleSeconds = 0.0;
  for (const auto& step : plan.steps) {
    if (const auto* loop = std::get_if<core::SetLoop>(&step.command); loop && loop->deck == req.incomingDeck) {
      if (loop->active) {
        ++incomingLoops;
        CHECK(loop->startSeconds == req.incomingDropSec);
      } else {
        released = step.beatOffset == req.durationBeats;
      }
    }
    if (const auto* seek = std::get_if<core::Seek>(&step.command); seek && seek->deck == req.incomingDeck) {
      seekToDrop = seek->seconds == req.incomingDropSec && step.beatOffset == req.durationBeats;
    }
    if (const auto* glide = std::get_if<core::GlideTempo>(&step.command); glide && glide->deck == req.incomingDeck) {
      CHECK(step.beatOffset == req.durationBeats);  // only after the mix, when the incoming track plays alone
      settleTo = glide->speed;
      settleSeconds = glide->seconds;
    }
    if (const auto* speed = std::get_if<core::SetPlaybackSpeed>(&step.command)) {
      CHECK((speed->deck != req.incomingDeck || step.beatOffset >= req.durationBeats));  // no tempo jump mid-mix
    }
  }
  CHECK(incomingLoops >= 4);
  CHECK(released);
  CHECK(seekToDrop);
  CHECK(settleTo == 1.0);        // the incoming ends at its own tempo...
  CHECK(settleSeconds >= 30.0);  // ...slowly enough that nobody hears it
}

TEST_CASE("TransitionPlanner: double drop and acapella stem blend", "[ai][transition][wave2]") {
  core::TransitionRequest req;
  req.durationBeats = 32.0;
  req.dropAtEnd = true;
  req.outgoingBpm = 174.0;
  req.incomingBpm = 174.0;

  SECTION("a double drop swaps the bass on the drop and keeps both tracks for four bars") {
    req.style = core::TransitionStyle::DoubleDrop;
    const auto plan = ai::TransitionPlanner{}.planTransition(req);
    CHECK(plan.durationBeats == 64.0);
    double outBassOut = -1.0;
    double outSilent = -1.0;
    for (const auto& step : plan.steps) {
      if (const auto* eq = std::get_if<core::SetEq>(&step.command);
          eq && eq->deck == req.outgoingDeck && eq->band == core::EqBand::Low && eq->db == -60.0F) {
        outBassOut = step.beatOffset;
      }
      if (const auto* v = std::get_if<core::SetVolume>(&step.command); v && v->deck == req.outgoingDeck && v->linear == 0.0F) {
        outSilent = step.beatOffset;
      }
    }
    CHECK(outBassOut == 32.0);
    CHECK(outSilent > 32.0 + 15.0);  // both drops together first
  }

  SECTION("an acapella blend leaves only the outgoing vocals over the incoming beat") {
    req.style = core::TransitionStyle::StemBlend;
    const auto plan = ai::TransitionPlanner{}.planTransition(req);
    int outMutesAtHalf = 0;
    bool incomingVocalsBack = false;
    for (const auto& step : plan.steps) {
      if (const auto* mute = std::get_if<core::SetStemMute>(&step.command)) {
        if (mute->deck == req.outgoingDeck && mute->muted && step.beatOffset == 16.0) ++outMutesAtHalf;
        if (mute->deck == req.incomingDeck && mute->stem == core::StemKind::Vocals && !mute->muted) incomingVocalsBack = true;
      }
    }
    CHECK(outMutesAtHalf == 3);  // drums, bass, other: the outgoing vocals are left alone
    CHECK(incomingVocalsBack);
  }
}

TEST_CASE("TransitionPlanner: echo out rings after the fader and is switched off after its tail", "[ai][transition][wave2]") {
  core::TransitionRequest req;
  req.style = core::TransitionStyle::EchoOut;
  req.durationBeats = 32.0;
  req.dropAtEnd = true;
  req.outgoingBpm = 174.0;
  req.incomingBpm = 174.0;
  const auto plan = ai::TransitionPlanner{}.planTransition(req);
  CHECK(plan.durationBeats == 40.0);
  double echoOn = -1.0;
  double echoOff = -1.0;
  double faderClosed = -1.0;
  for (const auto& step : plan.steps) {
    if (const auto* fx = std::get_if<core::SetFx>(&step.command); fx && fx->deck == req.outgoingDeck) {
      if (fx->type == core::FxType::Echo && fx->enabled) {
        echoOn = step.beatOffset;
        CHECK(fx->tailAfterFader);
      }
      if (fx->type == core::FxType::None) echoOff = step.beatOffset;
    }
    if (const auto* v = std::get_if<core::SetVolume>(&step.command); v && v->deck == req.outgoingDeck && v->linear == 0.0F) {
      faderClosed = step.beatOffset;
    }
  }
  CHECK(echoOn == 28.0);
  CHECK(faderClosed < 32.0);
  CHECK(echoOff == 40.0);  // the tail rang over the incoming drop first
}
