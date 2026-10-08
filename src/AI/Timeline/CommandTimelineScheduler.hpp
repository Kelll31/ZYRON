// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "Core/AI/TimelineTypes.hpp"
#include "Core/Commands/CommandBus.hpp"

namespace zyron::ai {

/// Beat-quantised command timeline scheduler with user-override-wins rule (ARCHITECTURE section 5, ROADMAP P8-01).
class CommandTimelineScheduler final : public core::ICommandTimelineScheduler, public core::CommandSink {
 public:
  using OverrideCallback = std::function<void(const core::TimelineOverrideEvent&)>;

  explicit CommandTimelineScheduler(core::CommandBus& commandBus);
  ~CommandTimelineScheduler() override = default;

  // ICommandTimelineScheduler overrides
  std::uint64_t scheduleAtBeat(
      core::Command command,
      double targetBeat,
      core::QuantiseGrid quantise = core::QuantiseGrid::None,
      std::string description = "") override;

  std::uint64_t scheduleAtTime(
      core::Command command,
      double targetTimeSec,
      std::string description = "") override;

  bool cancelCommand(std::uint64_t id) override;
  std::size_t cancelDeckCommands(core::DeckId deck) override;
  void clear() override;

  std::size_t advanceToBeats(core::DeckId masterDeck, double currentBeat) override;
  std::size_t advanceToTime(double currentTimeSec) override;

  void handleUserCommand(const core::Command& command, const core::CommandOrigin& origin) override;
  [[nodiscard]] bool isDeckOverridden(core::DeckId deck) const noexcept override;
  void clearDeckOverride(core::DeckId deck) override;

  [[nodiscard]] std::size_t pendingCount() const noexcept override;
  [[nodiscard]] std::size_t executedCount() const noexcept override;
  [[nodiscard]] std::size_t cancelledCount() const noexcept override;

  // CommandSink overrides (receives bus commands to detect user actions)
  void onCommand(const core::Command& command, const core::CommandOrigin& origin) noexcept override;

  void setOnOverrideCallback(OverrideCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onOverride_ = std::move(cb);
  }

  [[nodiscard]] const std::vector<core::TimelineOverrideEvent>& overrideHistory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return overrideHistory_;
  }

  /// A manual action holds the AI off a deck for this long, then the automation takes over again.
  static constexpr std::chrono::seconds kOverrideLifetime{10};

  /// Quantises a beat target forward to the next grid interval.
  [[nodiscard]] static double quantiseToGrid(double beat, core::QuantiseGrid grid) noexcept;

 private:
  core::CommandBus& commandBus_;
  mutable std::mutex mutex_;

  std::vector<core::ScheduledCommand> scheduled_;
  std::uint64_t nextId_{1};
  std::size_t executedCount_{0};
  std::size_t cancelledCount_{0};

  std::array<bool, core::kDeckCount> deckOverridden_{false, false, false, false};
  std::array<std::chrono::steady_clock::time_point, core::kDeckCount> overriddenAt_{};
  std::vector<core::TimelineOverrideEvent> overrideHistory_;
  OverrideCallback onOverride_;
};

}  // namespace zyron::ai
