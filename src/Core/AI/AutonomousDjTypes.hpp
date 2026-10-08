// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/AI/SetBuilderTypes.hpp"
#include "Core/AI/TransitionTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Lifecycle states of the autonomous AI DJ engine (SPEC section 58, ROADMAP P8-03).
enum class AutonomousDjStatus : std::uint8_t {
  Stopped = 0,
  Running,
  Paused,
  Finished,
  Error
};

/// Operational phase within a track or mix transition.
enum class AutonomousDjPhase : std::uint8_t {
  Idle = 0,
  LoadingFirstTrack,
  PlayingTrack,
  PreparingIncomingTrack,
  ExecutingTransition,
  Finished
};

/// Configuration parameters for autonomous DJ sets.
struct AutonomousDjConfig {
  std::string targetGenre{"Drum & Bass"};
  double targetDurationMinutes{60.0};
  double minBpm{160.0};
  double maxBpm{180.0};
  EnergyCurvePreset energyPreset{EnergyCurvePreset::PeakHour};
  TransitionStyle transitionStyle{TransitionStyle::BassSwap};
  double transitionDurationBeats{64.0};
  double leadTimeSeconds{30.0};          // Lead time before track end to initiate incoming mix
};

/// Telemetry status snapshot of the autonomous DJ loop.
struct AutonomousDjTelemetry {
  AutonomousDjStatus status{AutonomousDjStatus::Stopped};
  AutonomousDjPhase phase{AutonomousDjPhase::Idle};
  DeckId activeDeck{DeckId::A};
  DeckId nextDeck{DeckId::B};
  std::int64_t currentTrackId{0};
  std::int64_t nextTrackId{0};
  std::size_t currentTrackIndex{0};
  std::size_t totalTracksInSet{0};
  double elapsedMinutes{0.0};
  std::string statusMessage{"Autonomous DJ is idle"};
};

/// Abstract interface for autonomous set sequencing and mixing (SPEC section 58, ROADMAP P8-03).
class IAutonomousDj {
 public:
  virtual ~IAutonomousDj() = default;

  /// Initializes and begins autonomous set playback using music library catalog.
  virtual bool start(
      const AutonomousDjConfig& config,
      const std::vector<TrackItem>& catalog) = 0;

  /// Halts autonomous playback and cancels pending scheduled transitions.
  virtual void stop() = 0;

  /// Temporarily pauses the autonomous sequencing loop.
  virtual void pause() = 0;

  /// Resumes autonomous sequencing loop.
  virtual void resume() = 0;

  /// Advances the autonomous loop state based on active deck playback progress.
  virtual void update(
      double elapsedSeconds,
      double activeDeckRemainingSec,
      double activeDeckBeat) = 0;

  /// Returns current operational telemetry.
  [[nodiscard]] virtual AutonomousDjTelemetry telemetry() const noexcept = 0;
};

}  // namespace zyron::core
