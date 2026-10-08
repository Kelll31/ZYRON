// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "AI/Transition/TransitionPlanner.hpp"
#include "Core/AI/AutonomousDjTypes.hpp"
#include "Core/AI/SetBuilderTypes.hpp"
#include "Core/AI/TimelineTypes.hpp"
#include "Core/AI/TransitionTypes.hpp"
#include "Core/Audio/EngineView.hpp"
#include "Core/Commands/CommandBus.hpp"

namespace zyron::ai {

/// Autonomous AI DJ mixing loop (SPEC section 58, ROADMAP P8-03).
class AutonomousDjLoop final : public core::IAutonomousDj {
 public:
  AutonomousDjLoop(
      core::CommandBus& commandBus,
      core::ISetBuilder& setBuilder,
      core::ITransitionPlanner& transitionPlanner,
      core::ICommandTimelineScheduler& timelineScheduler);
  ~AutonomousDjLoop() override = default;

  bool start(
      const core::AutonomousDjConfig& config,
      const std::vector<core::TrackItem>& catalog) override;

  void stop() override;
  void pause() override;
  void resume() override;

  void update(
      double elapsedSeconds,
      double activeDeckRemainingSec,
      double activeDeckBeat) override;

  [[nodiscard]] core::AutonomousDjTelemetry telemetry() const noexcept override;

  /// With a load source the loop waits for decks to finish loading (decoding takes a moment) before it plays or mixes
  /// them. Without one (unit tests) loading is assumed to be instant.
  void setDeckLoadSource(const core::IDeckLoadSource* loads) noexcept { loads_ = loads; }

  /// Mix points (markers) of the tracks: with them the mix out starts at the track's mix-out point, the incoming track
  /// is started at its mix-in point and its drop is placed in the middle of the transition. Without them the fixed
  /// lead time of the configuration applies and the incoming track starts from the beginning.
  void setMixPointProvider(core::IMixPointProvider* provider) noexcept { mixPoints_ = provider; }

  /// Moves or removes a track that has not started and is not being prepared. False when that is not possible.
  bool moveUpcoming(std::size_t from, std::size_t to);
  bool removeUpcoming(std::size_t index);

  /// Puts `track` right after the playing one (a live "play this next"). With `mixNow` the transition starts as soon as
  /// the track has loaded instead of at the mix-out point. If the next track was already loading it is replaced; if the
  /// transition into it is already running, the track goes after that one. False when nothing is playing.
  bool insertNext(const core::TrackItem& track, bool mixNow);

  /// Snapshot of the plan for display: the tracks in order with the index of the one playing.
  [[nodiscard]] core::AutomixQueue queue() const;

  [[nodiscard]] const core::SetPlan& currentSetPlan() const noexcept { return setPlan_; }

  /// Where the playing deck is in its track (seconds) and how fast it plays; call before update(). Lets a transition
  /// know the outgoing position for loops and brakes.
  /// The transitions to use in turn and their length, from the mix profile; the next transition picks them up.
  void setStyleRotation(std::vector<core::TransitionStyle> styles, double transitionBeats, bool separateStems = false,
                        bool fxHits = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.styleRotation = std::move(styles);
    config_.transitionDurationBeats = transitionBeats;
    config_.separateStems = separateStems;
    config_.fxHits = fxHits;
  }

  bool setTransitionStyle(std::size_t index, std::optional<core::TransitionStyle> style);
  [[nodiscard]] core::TransitionPreview transitionPreview() const;

  /// How many decks the set may use (2 or 4, the layout the DJ sees); the next transition picks it up.
  void setDeckCount(std::size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    deckCount_ = count >= 4 ? 4 : 2;
    if (core::index(nextDeck_) >= deckCount_ && telemetry_.phase == core::AutonomousDjPhase::PlayingTrack) {
      nextDeck_ = static_cast<core::DeckId>((core::index(activeDeck_) + 1) % deckCount_);  // not loading yet: move it
    }
  }

  void setActiveDeckState(double positionSec, double speed, double incomingPositionSec = -1.0,
                          bool incomingPlaying = false) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    activePositionSec_ = positionSec;
    activeSpeed_ = speed;
    incomingPositionSec_ = incomingPositionSec;
    incomingPlaying_ = incomingPlaying;
  }

 private:
  core::CommandBus& commandBus_;
  core::ISetBuilder& setBuilder_;
  core::ITransitionPlanner& transitionPlanner_;
  core::ICommandTimelineScheduler& timelineScheduler_;

  const core::IDeckLoadSource* loads_{nullptr};
  core::IMixPointProvider* mixPoints_{nullptr};
  mutable std::mutex mutex_;
  core::AutonomousDjConfig config_;
  core::AutonomousDjTelemetry telemetry_;
  core::SetPlan setPlan_;

  core::DeckId activeDeck_{core::DeckId::A};
  core::DeckId nextDeck_{core::DeckId::B};
  std::size_t currentTrackIndex_{0};
  double transitionStartBeat_{0.0};
  double transitionBeats_{32.0};  // the length of the running transition (shorter when the incoming intro is)
  double firstTrackSeekSec_{-1.0};
  bool forceTransition_{false};  // start the transition as soon as the incoming deck is ready
  double activePositionSec_{-1.0};
  double activeSpeed_{1.0};
  std::size_t transitionCount_{0};
  std::size_t deckCount_{2};
  std::map<std::int64_t, core::TransitionStyle> chosenStyles_;  // by incoming track id
  core::TransitionPreview runningPreview_;  // what the running transition does (empty when none runs)
  double lastElapsedSec_{0.0};
  double incomingPositionSec_{-1.0};
  bool incomingPlaying_{false};
  double clockAnchorBeat_{0.0};   // the transition clock: beat and time when it was planned, and its rate
  double clockAnchorSec_{0.0};
  double clockBeatsPerSec_{0.0};
  bool clockFromIncoming_{true};  // false while the incoming deck loops (a beat loop-in): the outgoing keeps time

  void startTransition(double activeDeckBeat, double startBeat);

  /// Everything a transition decides before it starts: where the incoming track enters, how long it lasts, how the
  /// outgoing one leaves, and whether its next drop forces a shorter mix or a loop. Pure: used to run and to preview.
  struct Decision {
    TransitionPlanner::IncomingCue cue;
    core::TransitionStyle style{core::TransitionStyle::BassSwap};
    double outStartSec{-1.0};
    double outBeatSec{0.0};
    double inBeatSec{0.0};
    double loopStartSec{-1.0};
    double loopBeats{0.0};
    double inDropSec{-1.0};
    bool vocalClash{false};
    double energyDelta{0.0};
    std::string check{"clean"};
  };
  [[nodiscard]] Decision decide(double outStartSec) const;
  [[nodiscard]] core::TransitionPreview previewOf(const Decision& decision) const;
  /// The style of the transition into plan entry `index` (the DJ's choice, else the profile's rotation).
  [[nodiscard]] core::TransitionStyle styleFor(std::size_t index) const;
  /// First index of the plan that may still be changed (the next track is locked once it is loading).
  [[nodiscard]] std::size_t firstEditableIndex() const;
  /// Seconds before the end of the playing track at which its mix out begins.
  [[nodiscard]] double leadSeconds() const;
  /// Where the next transition starts in the playing track (s): its mix-out point, or for a double drop the bars
  /// before one of its drops. -1 when unknown.
  [[nodiscard]] double plannedStartSec() const;
  /// The outgoing drop a double drop would land on (s), or -1 when none fits from `fromSec`.
  [[nodiscard]] double doubleDropTarget(double fromSec) const;
  std::int64_t stemsRequestedFor_{0};  // the incoming track whose stems were already asked for
  [[nodiscard]] bool isLoaded(core::DeckId deck, std::int64_t trackId, std::string* failure) const;
};

}  // namespace zyron::ai
