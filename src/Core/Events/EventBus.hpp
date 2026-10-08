// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

#include "Core/Commands/CommandTypes.hpp"

namespace zyron::core {

/// The application state advanced. This is an invalidation HINT: read StateStore::snapshot() for the data. With
/// several producer threads the hints may be delivered out of order (`{6}` before `{5}`); the snapshot you read is
/// never older than the revision named in the event.
struct StateChanged {
  std::uint64_t revision{0};
};

/// A command failed validation; nothing changed.
struct CommandRejected {
  std::string command;  // wire name, e.g. "PLAY"
  CommandError error;
  CommandOrigin origin;
};

using Event = std::variant<StateChanged, CommandRejected>;

/// Publish/subscribe for facts that already happened. Non-realtime only.
///
/// Delivery: synchronous, on the thread that calls publish(), in subscription order. That may be any producer thread;
/// UI adapters must marshal to the message thread themselves. publish() takes no lock while handlers run, so handlers
/// may subscribe, unsubscribe or publish again.
///
/// Unsubscribe guarantee: when unsubscribe() returns, the handler is not running and will never be called again -
/// it waits for a delivery that is in flight on another thread. (A handler unsubscribing itself, or another one, from
/// inside a delivery does not wait for itself.) Consequence: a handler must not block on the thread that is calling
/// unsubscribe(), or the two deadlock.
///
/// Failures: a handler that throws is isolated - the remaining handlers still run, the failure is counted
/// (failedDeliveries()) and passed to the optional error handler. It never reaches the publisher.
class EventBus {
 public:
  using Handler = std::function<void(const Event&)>;
  using SubscriptionId = std::uint64_t;
  using ErrorHandler = std::function<void(std::exception_ptr)>;

  static constexpr SubscriptionId kNoSubscription = 0;

  EventBus();
  ~EventBus();
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  /// Registers a handler. An empty handler is refused: nothing is registered and kNoSubscription is returned.
  SubscriptionId subscribe(Handler handler);
  /// Unknown ids and kNoSubscription are ignored. See the class comment for the guarantee.
  void unsubscribe(SubscriptionId id);

  void publish(const Event& event) const;

  /// Optional sink for handler exceptions. Called on the publishing thread inside the failed delivery; must not throw.
  void setErrorHandler(ErrorHandler handler);
  [[nodiscard]] std::uint64_t failedDeliveries() const noexcept { return failures_.load(); }

 private:
  struct Entry {
    Entry(SubscriptionId entryId, Handler entryHandler) : id(entryId), handler(std::move(entryHandler)) {}

    const SubscriptionId id;
    const Handler handler;
    std::mutex mutex;
    std::condition_variable idle;
    bool active{true};  // guarded by `mutex`
    int running{0};     // deliveries currently inside `handler`, guarded by `mutex`
  };
  using EntryList = std::vector<std::shared_ptr<Entry>>;

  void deliver(Entry& entry, const Event& event) const;

  mutable std::mutex mutex_;                  // guards entries_ and errorHandler_
  std::shared_ptr<const EntryList> entries_;  // copy-on-write: publish() iterates a stable snapshot
  ErrorHandler errorHandler_;
  std::atomic<SubscriptionId> lastId_{0};
  mutable std::atomic<std::uint64_t> failures_{0};
};

}  // namespace zyron::core
