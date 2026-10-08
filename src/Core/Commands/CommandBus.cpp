// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/Commands/CommandBus.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace zyron::core {
namespace {

/// Records which thread owns the bus lock for the lifetime of the scope.
class OwnerScope {
 public:
  explicit OwnerScope(std::atomic<std::thread::id>& owner) : owner_(owner) { owner_.store(std::this_thread::get_id()); }
  ~OwnerScope() { owner_.store(std::thread::id{}); }
  OwnerScope(const OwnerScope&) = delete;
  OwnerScope& operator=(const OwnerScope&) = delete;

 private:
  std::atomic<std::thread::id>& owner_;
};

}  // namespace

CommandBus::CommandBus(StateStore& store, EventBus& events) : store_(store), events_(events) {}

bool CommandBus::insideSinkCallback() const noexcept {
  return owner_.load() == std::this_thread::get_id();
}

void CommandBus::addSink(std::shared_ptr<CommandSink> sink) {
  if (!sink || insideSinkCallback()) {
    return;
  }
  const std::scoped_lock lock(mutex_);
  const OwnerScope owner(owner_);
  sink->onAttach(*store_.snapshot());
  sinks_.push_back(std::move(sink));
}

void CommandBus::removeSink(const std::shared_ptr<CommandSink>& sink) {
  if (!sink || insideSinkCallback()) {
    return;
  }
  const std::scoped_lock lock(mutex_);
  std::erase(sinks_, sink);
}

std::optional<CommandError> CommandBus::submit(const Command& command, const CommandOrigin& origin) {
  if (insideSinkCallback()) {
    return CommandError{CommandErrorCode::Reentrant, "submit() must not be called from inside a CommandSink callback"};
  }

  std::optional<CommandError> error;
  std::uint64_t revision = 0;
  {
    const std::scoped_lock lock(mutex_);
    const OwnerScope owner(owner_);
    for (;;) {
      const auto current = store_.snapshot();
      error = validate(command, *current);
      if (error) {
        break;
      }
      AppState next = apply(*current, command);
      revision = next.revision;
      if (store_.publishIfRevision(current->revision, std::move(next))) {
        for (const auto& sink : sinks_) {
          sink->onCommand(command, origin);
        }
        break;
      }
      // Someone else wrote to the store behind our back; validate again against the fresh state.
    }
  }

  if (error) {
    events_.publish(CommandRejected{std::string(commandName(command)), *error, origin});
  } else {
    events_.publish(StateChanged{revision});
  }
  return error;
}

}  // namespace zyron::core
