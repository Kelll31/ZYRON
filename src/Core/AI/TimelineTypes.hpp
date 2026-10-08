// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandTypes.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Beat-quantization grid intervals for musical event alignment (SPEC section 50, ROADMAP P8-01).
enum class QuantiseGrid : std::uint8_t {
  None = 0,         // Trigger immediately / unquantized
  QuarterBeat,      // 1/4 beat (16th note)
  HalfBeat,         // 1/2 beat (8th note)
  Beat,             // 1 beat (quarter note)
  Bar,              // 1 bar (4 beats in 4/4)
  TwoBars,          // 2 bars (8 beats)
  FourBars,         // 4 bars (16 beats)
  Phrase8,          // 8 bars (32 beats)
  Phrase16,         // 16 bars (64 beats)
  Phrase32          // 32 bars (128 beats)
};

[[nodiscard]] constexpr double quantiseGridBeats(QuantiseGrid grid) noexcept {
  switch (grid) {
    case QuantiseGrid::None: return 0.0;
    case QuantiseGrid::QuarterBeat: return 0.25;
    case QuantiseGrid::HalfBeat: return 0.5;
    case QuantiseGrid::Beat: return 1.0;
    case QuantiseGrid::Bar: return 4.0;
    case QuantiseGrid::TwoBars: return 8.0;
    case QuantiseGrid::FourBars: return 16.0;
    case QuantiseGrid::Phrase8: return 32.0;
    case QuantiseGrid::Phrase16: return 64.0;
    case QuantiseGrid::Phrase32: return 128.0;
  }
  return 0.0;
}

/// A command scheduled on the musical timeline.
struct ScheduledCommand {
  std::uint64_t id{0};
  Command command{Play{DeckId::A}};
  CommandOrigin origin{CommandOrigin::Kind::Ai, "timeline"};
  double triggerBeat{0.0};             // Absolute or master deck beat position
  double triggerTimeSec{0.0};          // Optional wall-clock or deck time seconds
  bool isBeatBased{true};              // true = beat-aligned, false = timestamp-aligned
  std::string description;
  bool executed{false};
  bool cancelled{false};
};

/// Event record when a human DJ manual action overrides and cancels/pauses AI timeline actions (ARCHITECTURE section 5).
struct TimelineOverrideEvent {
  DeckId deck{DeckId::A};
  std::string commandName;
  CommandOrigin::Kind userOrigin{CommandOrigin::Kind::Ui};
  std::string userSourceId;
  double triggerBeat{0.0};
  std::size_t cancelledCommandsCount{0};
  std::string explanation;
};

/// Abstract interface for beat-quantised command scheduling (SPEC section 50, ARCHITECTURE section 5, ROADMAP P8-01).
class ICommandTimelineScheduler {
 public:
  virtual ~ICommandTimelineScheduler() = default;

  /// Schedules a command for future execution at target beat position.
  virtual std::uint64_t scheduleAtBeat(
      Command command,
      double targetBeat,
      QuantiseGrid quantise = QuantiseGrid::None,
      std::string description = "") = 0;

  /// Schedules a command for execution at target time (in seconds).
  virtual std::uint64_t scheduleAtTime(
      Command command,
      double targetTimeSec,
      std::string description = "") = 0;

  /// Cancels a scheduled command by its ID.
  virtual bool cancelCommand(std::uint64_t id) = 0;

  /// Cancels all pending AI commands targeting a specific deck (e.g. on manual user override).
  virtual std::size_t cancelDeckCommands(DeckId deck) = 0;

  /// Cancels all scheduled commands.
  virtual void clear() = 0;

  /// Advances the timeline to current master beat position, executing ready commands.
  virtual std::size_t advanceToBeats(DeckId masterDeck, double currentBeat) = 0;

  /// Advances the timeline to current wall-clock/playback seconds, executing ready time-based commands.
  virtual std::size_t advanceToTime(double currentTimeSec) = 0;

  /// Notifies the scheduler of a user-origin command to enforce user-override-wins rule.
  virtual void handleUserCommand(const Command& command, const CommandOrigin& origin) = 0;

  /// Checks if a deck currently has an active user override blocking AI automation.
  [[nodiscard]] virtual bool isDeckOverridden(DeckId deck) const noexcept = 0;

  /// Clears manual override status on a deck, allowing AI commands to resume.
  virtual void clearDeckOverride(DeckId deck) = 0;

  /// Number of currently pending scheduled commands.
  [[nodiscard]] virtual std::size_t pendingCount() const noexcept = 0;

  /// Total commands executed since start.
  [[nodiscard]] virtual std::size_t executedCount() const noexcept = 0;

  /// Total commands cancelled since start.
  [[nodiscard]] virtual std::size_t cancelledCount() const noexcept = 0;
};

}  // namespace zyron::core
