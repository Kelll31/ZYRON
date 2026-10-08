// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/State/AppState.hpp"

namespace zyron::core {

/// Receives every accepted command, after the state has been updated. The audio-engine bridge (ROADMAP P2-05) is
/// the first real implementation: it translates the command into a fixed-size realtime message.
///
/// Contract: both callbacks are `noexcept` (a sink that cannot fail must not throw into the bus - report problems
/// through its own counters/queues), must be quick, and must not block. Calling CommandBus::submit from inside a
/// callback is refused with CommandErrorCode::Reentrant; addSink/removeSink from inside one is ignored.
class CommandSink {
 public:
  virtual ~CommandSink() = default;

  /// Called once when the sink is registered, with the state as of that moment, so a late-registered sink (e.g. the
  /// engine bridge after a session restore) starts in sync instead of only hearing future changes.
  virtual void onAttach(const AppState& current) noexcept { (void)current; }

  virtual void onCommand(const Command& command, const CommandOrigin& origin) noexcept = 0;
};

/// The one entry point for UI, MIDI and AI (SPEC sections 8, 50, 51): validate, update state, forward, announce.
///
/// submit() is safe to call from any non-realtime thread; submissions are serialised, so every command is validated
/// against the state produced by the one before it, and sinks never run concurrently. Events are published after the
/// internal lock is released, so event handlers may call submit() again.
class CommandBus {
 public:
  CommandBus(StateStore& store, EventBus& events);

  /// Registers a sink (null is ignored) and calls its onAttach(). Sinks are called in registration order.
  void addSink(std::shared_ptr<CommandSink> sink);
  void removeSink(const std::shared_ptr<CommandSink>& sink);

  /// nullopt = accepted. Otherwise the state is untouched, no sink is called and CommandRejected is published
  /// (except for Reentrant, which is returned without publishing).
  [[nodiscard]] std::optional<CommandError> submit(const Command& command, const CommandOrigin& origin);

 private:
  [[nodiscard]] bool insideSinkCallback() const noexcept;

  StateStore& store_;
  EventBus& events_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<CommandSink>> sinks_;
  std::atomic<std::thread::id> owner_{};  // thread currently holding mutex_, to detect re-entrant calls
};

}  // namespace zyron::core
