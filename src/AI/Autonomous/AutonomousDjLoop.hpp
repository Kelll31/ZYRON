// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "Core/AI/AutonomousDjTypes.hpp"
#include "Core/AI/SetBuilderTypes.hpp"
#include "Core/AI/TimelineTypes.hpp"
#include "Core/AI/TransitionTypes.hpp"
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

  [[nodiscard]] const core::SetPlan& currentSetPlan() const noexcept { return setPlan_; }

 private:
  core::CommandBus& commandBus_;
  core::ISetBuilder& setBuilder_;
  core::ITransitionPlanner& transitionPlanner_;
  core::ICommandTimelineScheduler& timelineScheduler_;

  mutable std::mutex mutex_;
  core::AutonomousDjConfig config_;
  core::AutonomousDjTelemetry telemetry_;
  core::SetPlan setPlan_;

  core::DeckId activeDeck_{core::DeckId::A};
  core::DeckId nextDeck_{core::DeckId::B};
  std::size_t currentTrackIndex_{0};
  double transitionStartBeat_{0.0};
};

}  // namespace zyron::ai
