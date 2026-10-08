// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Autonomous/AutonomousDjLoop.hpp"

#include <utility>

namespace zyron::ai {

AutonomousDjLoop::AutonomousDjLoop(
    core::CommandBus& commandBus,
    core::ISetBuilder& setBuilder,
    core::ITransitionPlanner& transitionPlanner,
    core::ICommandTimelineScheduler& timelineScheduler)
    : commandBus_(commandBus),
      setBuilder_(setBuilder),
      transitionPlanner_(transitionPlanner),
      timelineScheduler_(timelineScheduler) {}

bool AutonomousDjLoop::start(
    const core::AutonomousDjConfig& config,
    const std::vector<core::TrackItem>& catalog) {
  std::lock_guard<std::mutex> lock(mutex_);

  core::SetBuilderRequest req;
  req.targetGenre = config.targetGenre;
  req.targetDurationMinutes = config.targetDurationMinutes;
  req.minBpm = config.minBpm;
  req.maxBpm = config.maxBpm;
  req.preset = config.energyPreset;

  setPlan_ = setBuilder_.buildSet(req, catalog);
  if (!setPlan_.success || setPlan_.tracks.empty()) {
    telemetry_.status = core::AutonomousDjStatus::Error;
    telemetry_.statusMessage = "Set planning failed: insufficient matching catalog tracks.";
    return false;
  }

  config_ = config;
  activeDeck_ = core::DeckId::A;
  nextDeck_ = core::DeckId::B;
  currentTrackIndex_ = 0;

  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "autonomous_dj"};

  // Clear timeline and prepare Deck A
  timelineScheduler_.clear();
  timelineScheduler_.clearDeckOverride(core::DeckId::A);
  timelineScheduler_.clearDeckOverride(core::DeckId::B);

  const auto firstTrackId = setPlan_.tracks[0].track.id;
  (void)commandBus_.submit(core::LoadTrack{activeDeck_, core::TrackId{firstTrackId}}, origin);
  (void)commandBus_.submit(core::SetVolume{activeDeck_, 1.0f}, origin);
  (void)commandBus_.submit(core::SetEq{activeDeck_, core::EqBand::Low, 0.0f}, origin);
  (void)commandBus_.submit(core::SetEq{activeDeck_, core::EqBand::Mid, 0.0f}, origin);
  (void)commandBus_.submit(core::SetEq{activeDeck_, core::EqBand::High, 0.0f}, origin);
  (void)commandBus_.submit(core::Play{activeDeck_}, origin);

  telemetry_.status = core::AutonomousDjStatus::Running;
  telemetry_.phase = core::AutonomousDjPhase::PlayingTrack;
  telemetry_.activeDeck = activeDeck_;
  telemetry_.nextDeck = nextDeck_;
  telemetry_.currentTrackId = firstTrackId;
  telemetry_.currentTrackIndex = 1;
  telemetry_.totalTracksInSet = setPlan_.tracks.size();
  telemetry_.elapsedMinutes = 0.0;
  telemetry_.statusMessage = "Playing track 1 of " + std::to_string(setPlan_.tracks.size()) +
                             ": " + setPlan_.tracks[0].track.title;

  return true;
}

void AutonomousDjLoop::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "autonomous_dj"};
  (void)commandBus_.submit(core::Pause{core::DeckId::A}, origin);
  (void)commandBus_.submit(core::Pause{core::DeckId::B}, origin);
  timelineScheduler_.clear();

  telemetry_.status = core::AutonomousDjStatus::Stopped;
  telemetry_.phase = core::AutonomousDjPhase::Idle;
  telemetry_.statusMessage = "Autonomous DJ stopped.";
}

void AutonomousDjLoop::pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.status == core::AutonomousDjStatus::Running) {
    telemetry_.status = core::AutonomousDjStatus::Paused;
    telemetry_.statusMessage = "Autonomous DJ paused.";
  }
}

void AutonomousDjLoop::resume() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.status == core::AutonomousDjStatus::Paused) {
    telemetry_.status = core::AutonomousDjStatus::Running;
    telemetry_.statusMessage = "Autonomous DJ resumed.";
  }
}

void AutonomousDjLoop::update(
    double elapsedSeconds,
    double activeDeckRemainingSec,
    double activeDeckBeat) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (telemetry_.status != core::AutonomousDjStatus::Running) {
    return;
  }

  telemetry_.elapsedMinutes = elapsedSeconds / 60.0;

  // Advance scheduled timeline events
  timelineScheduler_.advanceToBeats(activeDeck_, activeDeckBeat);

  // Check user override
  if (timelineScheduler_.isDeckOverridden(activeDeck_)) {
    telemetry_.statusMessage = "User manual control active on Deck " +
                               std::to_string(static_cast<int>(core::index(activeDeck_)) + 1);
    return;
  }

  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "autonomous_dj"};

  if (telemetry_.phase == core::AutonomousDjPhase::PlayingTrack) {
    // Check if we reached the mix lead-in point
    if (activeDeckRemainingSec <= config_.leadTimeSeconds && (currentTrackIndex_ + 1) < setPlan_.tracks.size()) {
      const auto& nextTrack = setPlan_.tracks[currentTrackIndex_ + 1].track;

      // Prepare incoming deck
      (void)commandBus_.submit(core::LoadTrack{nextDeck_, core::TrackId{nextTrack.id}}, origin);

      // Plan transition
      core::TransitionRequest req;
      req.outgoingDeck = activeDeck_;
      req.incomingDeck = nextDeck_;
      req.style = config_.transitionStyle;
      req.startBeat = activeDeckBeat;
      req.durationBeats = config_.transitionDurationBeats;

      const auto plan = transitionPlanner_.planTransition(req);
      transitionPlanner_.scheduleTransition(plan, timelineScheduler_);

      transitionStartBeat_ = activeDeckBeat;
      telemetry_.phase = core::AutonomousDjPhase::ExecutingTransition;
      telemetry_.nextTrackId = nextTrack.id;
      telemetry_.statusMessage = "Mixing to next track (" + nextTrack.title + ") via " +
                                 std::string(core::transitionStyleName(config_.transitionStyle));
    }
  } else if (telemetry_.phase == core::AutonomousDjPhase::ExecutingTransition) {
    const double transitionEndBeat = transitionStartBeat_ + config_.transitionDurationBeats;
    if (activeDeckBeat >= transitionEndBeat || activeDeckRemainingSec <= 1.0) {
      // Transition completed: swap decks
      std::swap(activeDeck_, nextDeck_);
      ++currentTrackIndex_;

      telemetry_.activeDeck = activeDeck_;
      telemetry_.nextDeck = nextDeck_;
      telemetry_.currentTrackIndex = currentTrackIndex_ + 1;
      telemetry_.currentTrackId = setPlan_.tracks[currentTrackIndex_].track.id;
      telemetry_.nextTrackId = 0;

      if (currentTrackIndex_ + 1 >= setPlan_.tracks.size()) {
        telemetry_.phase = core::AutonomousDjPhase::Finished;
        telemetry_.status = core::AutonomousDjStatus::Finished;
        telemetry_.statusMessage = "Autonomous DJ set completed successfully.";
      } else {
        telemetry_.phase = core::AutonomousDjPhase::PlayingTrack;
        telemetry_.statusMessage = "Playing track " + std::to_string(currentTrackIndex_ + 1) +
                                   " of " + std::to_string(setPlan_.tracks.size()) + ": " +
                                   setPlan_.tracks[currentTrackIndex_].track.title;
      }
    }
  }
}

core::AutonomousDjTelemetry AutonomousDjLoop::telemetry() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return telemetry_;
}

}  // namespace zyron::ai
