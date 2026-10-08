// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/Events/EventBus.hpp"

#include <algorithm>
#include <utility>

namespace zyron::core {
namespace {

/// The entries whose handlers are currently executing on this thread (innermost last). Lets unsubscribe() recognise
/// a handler that is removing itself and avoid waiting for its own delivery.
std::vector<const void*>& deliveringOnThisThread() {
  thread_local std::vector<const void*> stack;
  return stack;
}

class DeliveryScope {
 public:
  explicit DeliveryScope(const void* entry) { deliveringOnThisThread().push_back(entry); }
  ~DeliveryScope() { deliveringOnThisThread().pop_back(); }
  DeliveryScope(const DeliveryScope&) = delete;
  DeliveryScope& operator=(const DeliveryScope&) = delete;
};

}  // namespace

EventBus::EventBus() : entries_(std::make_shared<const EntryList>()) {}

EventBus::~EventBus() = default;

EventBus::SubscriptionId EventBus::subscribe(Handler handler) {
  if (!handler) {
    return kNoSubscription;
  }
  const SubscriptionId id = ++lastId_;
  auto entry = std::make_shared<Entry>(id, std::move(handler));

  std::shared_ptr<const EntryList> retired;  // destroyed after the lock is released
  {
    const std::scoped_lock lock(mutex_);
    auto next = std::make_shared<EntryList>(*entries_);
    next->push_back(std::move(entry));
    retired = std::move(entries_);
    entries_ = std::move(next);
  }
  return id;
}

void EventBus::unsubscribe(SubscriptionId id) {
  std::shared_ptr<Entry> removed;
  std::shared_ptr<const EntryList> retired;  // destroyed after the lock is released
  {
    const std::scoped_lock lock(mutex_);
    const auto it = std::find_if(entries_->begin(), entries_->end(), [id](const auto& e) { return e->id == id; });
    if (it == entries_->end()) {
      return;
    }
    removed = *it;
    auto next = std::make_shared<EntryList>(*entries_);
    next->erase(next->begin() + (it - entries_->begin()));
    retired = std::move(entries_);
    entries_ = std::move(next);
  }

  // No bus lock is held from here on: wait for deliveries still inside the handler, except our own.
  const auto& stack = deliveringOnThisThread();
  const int ownDeliveries =
      static_cast<int>(std::count(stack.begin(), stack.end(), static_cast<const void*>(removed.get())));
  std::unique_lock lock(removed->mutex);
  removed->active = false;
  removed->idle.wait(lock, [&] { return removed->running <= ownDeliveries; });
}

void EventBus::setErrorHandler(ErrorHandler handler) {
  const std::scoped_lock lock(mutex_);
  errorHandler_ = std::move(handler);
}

void EventBus::publish(const Event& event) const {
  std::shared_ptr<const EntryList> snapshot;
  {
    const std::scoped_lock lock(mutex_);
    snapshot = entries_;
  }
  for (const auto& entry : *snapshot) {
    deliver(*entry, event);
  }
}

void EventBus::deliver(Entry& entry, const Event& event) const {
  {
    const std::scoped_lock lock(entry.mutex);
    if (!entry.active) {
      return;  // unsubscribed after this publish took its snapshot
    }
    ++entry.running;
  }

  try {
    const DeliveryScope scope(&entry);
    entry.handler(event);
  } catch (...) {
    failures_.fetch_add(1);
    ErrorHandler report;
    {
      const std::scoped_lock lock(mutex_);
      report = errorHandler_;
    }
    if (report) {
      report(std::current_exception());
    }
  }

  {
    const std::scoped_lock lock(entry.mutex);
    --entry.running;
  }
  entry.idle.notify_all();
}

}  // namespace zyron::core
