// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_events/juce_events.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <string>

#include "AI/Autonomous/AutonomousDjLoop.hpp"
#include "AI/SetBuilder/SetBuilder.hpp"
#include "AI/Timeline/CommandTimelineScheduler.hpp"
#include "AI/Transition/TransitionPlanner.hpp"
#include "Audio/Engine/AudioEngine.hpp"
#include "Core/AI/AutonomousDjTypes.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Library/LibraryService.hpp"

namespace zyron::application {

/// Automix: plans a set from the analysed tracks of the library and mixes it through the Command API, like a DJ would
/// (SPEC section 58). The AI module (AutonomousDjLoop) decides what to do; this class feeds it the real state of the
/// decks ten times a second - how long the playing track has left and where its beat is - and wires the loop to the
/// command bus and the library.
///
/// Everything runs on the message thread (a juce::Timer), the same thread that reads the engine telemetry. A manual
/// action of the user on a deck wins over the AI on that deck (the timeline scheduler cancels its pending commands).
class AutomixController final : public core::IAutomixControl, public core::IMixPointProvider, private juce::Timer {
 public:
  AutomixController(core::CommandBus& bus, audio::AudioEngine& engine, library::LibraryService& library);
  ~AutomixController() override;

  AutomixController(const AutomixController&) = delete;
  AutomixController& operator=(const AutomixController&) = delete;

  // core::IAutomixControl
  bool start() override;
  void stop() override;
  [[nodiscard]] core::AutonomousDjTelemetry telemetry() const override;
  [[nodiscard]] core::AutomixQueue queue() const override;
  bool moveUpcoming(std::size_t from, std::size_t to) override { return loop_.moveUpcoming(from, to); }
  bool removeUpcoming(std::size_t index) override { return loop_.removeUpcoming(index); }

  std::string mixNext(std::int64_t trackId) override;
  bool jumpToTransition() override;
  [[nodiscard]] std::string liveMixStatus() const override { return liveMessage_; }
  void setMixProfile(const core::MixProfile& profile) override;
  void setVisibleDeckCount(std::size_t count) override {
    visibleDecks_ = count >= 4 ? 4 : 2;
    loop_.setDeckCount(visibleDecks_);
  }
  bool setTransitionStyle(std::size_t index, std::optional<core::TransitionStyle> style) override {
    return loop_.setTransitionStyle(index, style);
  }
  [[nodiscard]] core::TransitionPreview transitionPreview() const override { return loop_.transitionPreview(); }

  // core::IMixPointProvider: the markers of a track, the user's own first, then the AI's proposals.
  [[nodiscard]] core::TrackMixPoints mixPoints(std::int64_t trackId) override;

  /// One control step: reads the decks and advances the loop. Called by the timer; tests call it directly.
  void tick();

 private:
  struct DeckGrid {
    std::int64_t trackId{0};
    double bpm{0.0};
    double firstBeatSec{0.0};
  };

  void timerCallback() override;
  /// A live mix without Automix: load the track on a free deck, then sync it and mix the playing deck into it.
  struct LiveMix {
    enum class Phase { Idle, Loading, Mixing } phase{Phase::Idle};
    core::DeckId outgoing{core::DeckId::A};
    core::DeckId incoming{core::DeckId::B};
    bool hasOutgoing{false};
    core::TrackItem track;
    double endBeat{0.0};
    std::chrono::steady_clock::time_point started;
  };
  [[nodiscard]] std::string startLiveMix(const core::TrackItem& track);
  void tickLiveMix();
  [[nodiscard]] double beatOf(core::DeckId deck, const core::LiveEngineState& live);
  [[nodiscard]] const DeckGrid& gridOf(core::DeckId deck, std::int64_t trackId);

  core::CommandBus& bus_;
  audio::AudioEngine& engine_;
  library::LibraryService& library_;

  ai::SetBuilder setBuilder_;
  ai::TransitionPlanner planner_;
  std::shared_ptr<ai::CommandTimelineScheduler> scheduler_;  // a bus sink: it sees the user's manual actions
  ai::AutonomousDjLoop loop_;

  std::chrono::steady_clock::time_point startedAt_;
  std::array<DeckGrid, core::kDeckCount> grids_{};
  struct CachedPoints {
    std::chrono::steady_clock::time_point at;
    core::TrackMixPoints points;
  };
  std::mutex pointsMutex_;
  std::unordered_map<std::int64_t, CachedPoints> pointsCache_;  // markers change rarely; the loop asks every tick
  LiveMix liveMix_;
  std::size_t visibleDecks_{2};  // decks C and D are off in the two-deck layout
  core::MixProfile profile_{core::MixProfile::preset(core::MixMode::Club)};
  std::optional<core::DeckId> restoreDeck_;  // a deck whose fader a finished live mix left down
  std::chrono::steady_clock::time_point restoreAt_;
  std::string liveMessage_;
  std::chrono::steady_clock::time_point liveMessageUntil_;
  std::string startFailure_;  // why the last start() was refused, shown instead of the loop's idle message
};

}  // namespace zyron::application
