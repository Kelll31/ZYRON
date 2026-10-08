// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Timeline/CommandTimelineScheduler.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::ai {

CommandTimelineScheduler::CommandTimelineScheduler(core::CommandBus& commandBus)
    : commandBus_(commandBus) {}

double CommandTimelineScheduler::quantiseToGrid(double beat, core::QuantiseGrid grid) noexcept {
  const double step = core::quantiseGridBeats(grid);
  if (step <= 0.0) {
    return beat;
  }
  // Round up to next grid boundary
  return std::ceil(beat / step) * step;
}

std::uint64_t CommandTimelineScheduler::scheduleAtBeat(
    core::Command command,
    double targetBeat,
    core::QuantiseGrid quantise,
    std::string description) {
  std::lock_guard<std::mutex> lock(mutex_);

  const double alignedBeat = quantiseToGrid(targetBeat, quantise);

  core::ScheduledCommand item;
  item.id = nextId_++;
  item.command = std::move(command);
  item.origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "timeline"};
  item.triggerBeat = alignedBeat;
  item.isBeatBased = true;
  item.description = std::move(description);
  item.executed = false;
  item.cancelled = false;

  scheduled_.push_back(std::move(item));

  // Keep ordered by triggerBeat
  std::stable_sort(scheduled_.begin(), scheduled_.end(), [](const auto& a, const auto& b) {
    return a.triggerBeat < b.triggerBeat;
  });

  return item.id;
}

std::uint64_t CommandTimelineScheduler::scheduleAtTime(
    core::Command command,
    double targetTimeSec,
    std::string description) {
  std::lock_guard<std::mutex> lock(mutex_);

  core::ScheduledCommand item;
  item.id = nextId_++;
  item.command = std::move(command);
  item.origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "timeline"};
  item.triggerTimeSec = targetTimeSec;
  item.isBeatBased = false;
  item.description = std::move(description);
  item.executed = false;
  item.cancelled = false;

  scheduled_.push_back(std::move(item));

  return item.id;
}

bool CommandTimelineScheduler::cancelCommand(std::uint64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& item : scheduled_) {
    if (item.id == id && !item.executed && !item.cancelled) {
      item.cancelled = true;
      ++cancelledCount_;
      return true;
    }
  }
  return false;
}

std::size_t CommandTimelineScheduler::cancelDeckCommands(core::DeckId deck) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t count = 0;
  for (auto& item : scheduled_) {
    if (!item.executed && !item.cancelled) {
      const auto target = core::targetDeck(item.command);
      if (target.has_value() && *target == deck) {
        item.cancelled = true;
        ++cancelledCount_;
        ++count;
      }
    }
  }
  return count;
}

void CommandTimelineScheduler::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& item : scheduled_) {
    if (!item.executed && !item.cancelled) {
      item.cancelled = true;
      ++cancelledCount_;
    }
  }
  scheduled_.clear();
}

std::size_t CommandTimelineScheduler::advanceToBeats(core::DeckId /*masterDeck*/, double currentBeat) {
  std::vector<core::ScheduledCommand> ready;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : scheduled_) {
      if (item.isBeatBased && !item.executed && !item.cancelled && item.triggerBeat <= currentBeat) {
        const auto target = core::targetDeck(item.command);
        if (target.has_value() && isDeckOverridden(*target)) {
          // Deck is currently overridden by human DJ; cancel this AI action
          item.cancelled = true;
          ++cancelledCount_;
          continue;
        }

        item.executed = true;
        ++executedCount_;
        ready.push_back(item);
      }
    }

    // Clean up finished/cancelled items
    scheduled_.erase(
        std::remove_if(
            scheduled_.begin(),
            scheduled_.end(),
            [](const auto& it) { return it.executed || it.cancelled; }),
        scheduled_.end());
  }

  // Submit outside mutex to avoid lock inversion
  for (const auto& item : ready) {
    (void)commandBus_.submit(item.command, item.origin);
  }

  return ready.size();
}

std::size_t CommandTimelineScheduler::advanceToTime(double currentTimeSec) {
  std::vector<core::ScheduledCommand> ready;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : scheduled_) {
      if (!item.isBeatBased && !item.executed && !item.cancelled && item.triggerTimeSec <= currentTimeSec) {
        const auto target = core::targetDeck(item.command);
        if (target.has_value() && isDeckOverridden(*target)) {
          item.cancelled = true;
          ++cancelledCount_;
          continue;
        }

        item.executed = true;
        ++executedCount_;
        ready.push_back(item);
      }
    }

    scheduled_.erase(
        std::remove_if(
            scheduled_.begin(),
            scheduled_.end(),
            [](const auto& it) { return it.executed || it.cancelled; }),
        scheduled_.end());
  }

  for (const auto& item : ready) {
    (void)commandBus_.submit(item.command, item.origin);
  }

  return ready.size();
}

void CommandTimelineScheduler::handleUserCommand(
    const core::Command& command, const core::CommandOrigin& origin) {
  if (origin.kind != core::CommandOrigin::Kind::Ui && origin.kind != core::CommandOrigin::Kind::Midi) {
    return;
  }

  const auto target = core::targetDeck(command);
  if (!target.has_value()) {
    return;
  }

  const auto deck = *target;
  std::size_t cancelledCount = 0;
  OverrideCallback cb;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    deckOverridden_[core::index(deck)] = true;

    // Immediately cancel pending AI commands on this deck (User-override-wins rule)
    for (auto& item : scheduled_) {
      if (!item.executed && !item.cancelled) {
        const auto cmdTarget = core::targetDeck(item.command);
        if (cmdTarget.has_value() && *cmdTarget == deck) {
          item.cancelled = true;
          ++cancelledCount_;
          ++cancelledCount;
        }
      }
    }

    core::TimelineOverrideEvent evt;
    evt.deck = deck;
    evt.commandName = std::string(core::commandName(command));
    evt.userOrigin = origin.kind;
    evt.userSourceId = origin.sourceId;
    evt.cancelledCommandsCount = cancelledCount;
    evt.explanation = "User manual control (" + evt.commandName + ") overrode AI automation on Deck " +
                      std::to_string(static_cast<int>(core::index(deck)) + 1);

    overrideHistory_.push_back(evt);
    cb = onOverride_;
  }

  if (cb) {
    core::TimelineOverrideEvent evt;
    evt.deck = deck;
    evt.commandName = std::string(core::commandName(command));
    evt.userOrigin = origin.kind;
    evt.userSourceId = origin.sourceId;
    evt.cancelledCommandsCount = cancelledCount;
    evt.explanation = "User manual control overrode AI automation on Deck";
    cb(evt);
  }
}

bool CommandTimelineScheduler::isDeckOverridden(core::DeckId deck) const noexcept {
  return deckOverridden_[core::index(deck)];
}

void CommandTimelineScheduler::clearDeckOverride(core::DeckId deck) {
  std::lock_guard<std::mutex> lock(mutex_);
  deckOverridden_[core::index(deck)] = false;
}

std::size_t CommandTimelineScheduler::pendingCount() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t count = 0;
  for (const auto& item : scheduled_) {
    if (!item.executed && !item.cancelled) {
      ++count;
    }
  }
  return count;
}

std::size_t CommandTimelineScheduler::executedCount() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return executedCount_;
}

std::size_t CommandTimelineScheduler::cancelledCount() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return cancelledCount_;
}

void CommandTimelineScheduler::onCommand(
    const core::Command& command, const core::CommandOrigin& origin) noexcept {
  try {
    handleUserCommand(command, origin);
  } catch (...) {
    // Sink callbacks are strictly noexcept
  }
}

}  // namespace zyron::ai
