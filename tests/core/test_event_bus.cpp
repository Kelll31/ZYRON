// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "Core/Events/EventBus.hpp"
#include "support/ThreadGroup.hpp"

using namespace zyron::core;

TEST_CASE("publish with no subscribers is a no-op") {
  const EventBus bus{};
  CHECK_NOTHROW(bus.publish(StateChanged{1}));
}

TEST_CASE("subscribers receive events in subscription order") {
  EventBus bus;
  std::vector<std::string> log;
  bus.subscribe([&](const Event&) { log.emplace_back("first"); });
  bus.subscribe([&](const Event&) { log.emplace_back("second"); });

  bus.publish(StateChanged{7});

  CHECK(log == std::vector<std::string>{"first", "second"});
}

TEST_CASE("the delivered event keeps its alternative and payload") {
  EventBus bus;
  std::uint64_t revision = 0;
  bus.subscribe([&](const Event& event) {
    if (const auto* changed = std::get_if<StateChanged>(&event)) {
      revision = changed->revision;
    }
  });

  bus.publish(StateChanged{42});

  CHECK(revision == 42);
}

TEST_CASE("an empty handler is refused instead of failing later at publish time") {
  EventBus bus;
  CHECK(bus.subscribe(EventBus::Handler{}) == EventBus::kNoSubscription);
  CHECK_NOTHROW(bus.publish(StateChanged{1}));
  CHECK(bus.failedDeliveries() == 0U);
}

TEST_CASE("unsubscribe stops delivery to that subscriber only") {
  EventBus bus;
  int a = 0;
  int b = 0;
  const auto idA = bus.subscribe([&](const Event&) { ++a; });
  bus.subscribe([&](const Event&) { ++b; });

  bus.publish(StateChanged{1});
  bus.unsubscribe(idA);
  bus.publish(StateChanged{2});

  CHECK(a == 1);
  CHECK(b == 2);
}

TEST_CASE("unsubscribing an unknown id or the no-subscription id is harmless") {
  EventBus bus;
  CHECK_NOTHROW(bus.unsubscribe(12345));
  CHECK_NOTHROW(bus.unsubscribe(EventBus::kNoSubscription));
}

TEST_CASE("a handler may unsubscribe itself while being called") {
  EventBus bus;
  int calls = 0;
  EventBus::SubscriptionId id = 0;
  id = bus.subscribe([&](const Event&) {
    ++calls;
    bus.unsubscribe(id);
  });

  bus.publish(StateChanged{1});
  bus.publish(StateChanged{2});

  CHECK(calls == 1);
}

TEST_CASE("a handler unsubscribed by an earlier handler of the same publish is not called") {
  EventBus bus;
  int victimCalls = 0;
  EventBus::SubscriptionId victim = 0;
  bus.subscribe([&](const Event&) { bus.unsubscribe(victim); });
  victim = bus.subscribe([&](const Event&) { ++victimCalls; });

  bus.publish(StateChanged{1});

  CHECK(victimCalls == 0);
}

TEST_CASE("a handler may subscribe another handler while being called") {
  EventBus bus;
  int inner = 0;
  bool added = false;
  bus.subscribe([&](const Event&) {
    if (!added) {
      added = true;
      bus.subscribe([&](const Event&) { ++inner; });
    }
  });

  bus.publish(StateChanged{1});  // the new handler is not part of this delivery
  CHECK(inner == 0);
  bus.publish(StateChanged{2});
  CHECK(inner == 1);
}

TEST_CASE("a handler may publish again (re-entrant delivery)") {
  EventBus bus;
  int depth = 0;
  int deepest = 0;
  bus.subscribe([&](const Event& event) {
    const auto* changed = std::get_if<StateChanged>(&event);
    if (changed != nullptr && changed->revision < 3) {
      ++depth;
      deepest = std::max(deepest, depth);
      bus.publish(StateChanged{changed->revision + 1});
      --depth;
    }
  });

  bus.publish(StateChanged{0});

  CHECK(deepest == 3);
}

TEST_CASE("a throwing handler does not stop the others and is counted and reported") {
  EventBus bus;
  int reached = 0;
  int reports = 0;
  bus.setErrorHandler([&](std::exception_ptr) { ++reports; });
  bus.subscribe([](const Event&) { throw std::runtime_error("boom"); });
  bus.subscribe([&](const Event&) { ++reached; });

  CHECK_NOTHROW(bus.publish(StateChanged{1}));

  CHECK(reached == 1);
  CHECK(bus.failedDeliveries() == 1U);
  CHECK(reports == 1);
}

TEST_CASE("unsubscribe from another thread waits for a handler that is still running") {
  EventBus bus;
  std::atomic<bool> entered{false};
  std::atomic<bool> release{false};
  std::atomic<bool> unsubscribed{false};
  std::atomic<int> callsAfterUnsubscribe{0};

  const auto id = bus.subscribe([&](const Event&) {
    if (unsubscribed.load()) {
      callsAfterUnsubscribe.fetch_add(1);
    }
    entered.store(true);
    while (!release.load()) {
      std::this_thread::yield();
    }
  });

  zyron::test::ThreadGroup threads;
  threads.spawn([&] { bus.publish(StateChanged{1}); });
  while (!entered.load()) {
    std::this_thread::yield();
  }

  threads.spawn([&] {
    bus.unsubscribe(id);
    unsubscribed.store(true);
  });

  // Negative check with a bounded wait: while the handler is still inside, unsubscribe() must not have returned.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK_FALSE(unsubscribed.load());

  release.store(true);
  threads.joinAll();

  CHECK(unsubscribed.load());
  bus.publish(StateChanged{2});
  CHECK(callsAfterUnsubscribe.load() == 0);
}

TEST_CASE("concurrent subscribe, unsubscribe and publish do not crash or lose live handlers") {
  EventBus bus;
  std::atomic<int> stableCalls{0};
  bus.subscribe([&](const Event&) { stableCalls.fetch_add(1); });

  constexpr int kPublishes = 500;
  zyron::test::ThreadGroup threads;
  threads.spawn([&] {
    for (int i = 0; i < kPublishes; ++i) {
      bus.publish(StateChanged{static_cast<std::uint64_t>(i)});
    }
  });
  for (int t = 0; t < 2; ++t) {
    threads.spawn([&] {
      for (int i = 0; i < 200; ++i) {
        const auto id = bus.subscribe([](const Event&) {});
        bus.unsubscribe(id);
      }
    });
  }
  threads.joinAll();

  CHECK(stableCalls.load() == kPublishes);
}
