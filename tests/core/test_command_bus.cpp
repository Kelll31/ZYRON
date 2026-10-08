// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "Core/Commands/CommandBus.hpp"
#include "support/ThreadGroup.hpp"

using namespace zyron::core;

namespace {

class RecordingSink final : public CommandSink {
 public:
  void onAttach(const AppState& current) noexcept override { attachedRevision_.store(current.revision); }

  void onCommand(const Command& command, const CommandOrigin& origin) noexcept override {
    // Detects two threads inside the sink at once; the bus must serialise calls.
    if (inFlight_.fetch_add(1) != 0) {
      overlapped_.store(true);
    }
    {
      const std::scoped_lock lock(mutex_);
      names_.emplace_back(commandName(command));
      origins_.push_back(origin);
    }
    inFlight_.fetch_sub(1);
  }

  [[nodiscard]] std::size_t count() const {
    const std::scoped_lock lock(mutex_);
    return names_.size();
  }
  [[nodiscard]] std::vector<std::string> names() const {
    const std::scoped_lock lock(mutex_);
    return names_;
  }
  [[nodiscard]] std::vector<CommandOrigin> origins() const {
    const std::scoped_lock lock(mutex_);
    return origins_;
  }
  [[nodiscard]] bool overlapped() const { return overlapped_.load(); }
  [[nodiscard]] std::uint64_t attachedRevision() const { return attachedRevision_.load(); }

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> names_;
  std::vector<CommandOrigin> origins_;
  std::atomic<int> inFlight_{0};
  std::atomic<bool> overlapped_{false};
  std::atomic<std::uint64_t> attachedRevision_{0};
};

/// A sink that tries to call back into the bus from inside onCommand(), which would deadlock a naive bus.
class ReentrantSink final : public CommandSink {
 public:
  explicit ReentrantSink(CommandBus& bus) : bus_(bus) {}

  void onCommand(const Command&, const CommandOrigin&) noexcept override {
    result = bus_.submit(SetVolume{DeckId::B, 0.5F}, CommandOrigin{CommandOrigin::Kind::Script, "reentrant"});
    ++calls;
  }

  std::optional<CommandError> result;
  int calls{0};

 private:
  CommandBus& bus_;
};

struct Fixture {
  StateStore store;
  EventBus events;
  CommandBus bus{store, events};
  std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();
  std::vector<Event> received;

  Fixture() {
    bus.addSink(sink);
    events.subscribe([this](const Event& event) { received.push_back(event); });
  }
};

const CommandOrigin kUi{CommandOrigin::Kind::Ui, "deck-a-play"};

}  // namespace

TEST_CASE("an accepted command updates state, reaches the sink once and announces the new revision") {
  Fixture f;

  const auto error = f.bus.submit(LoadTrack{DeckId::A, TrackId{10}}, kUi);

  CHECK_FALSE(error.has_value());
  const auto snapshot = f.store.snapshot();
  CHECK(snapshot->revision == 1);
  CHECK(snapshot->deck(DeckId::A).track == TrackId{10});

  REQUIRE(f.sink->count() == 1U);
  CHECK(f.sink->names().front() == "LOAD_TRACK");
  CHECK(f.sink->origins().front().sourceId == "deck-a-play");

  REQUIRE(f.received.size() == 1U);
  const auto* changed = std::get_if<StateChanged>(&f.received.front());
  REQUIRE(changed != nullptr);
  CHECK(changed->revision == 1);
}

TEST_CASE("a rejected command changes nothing, skips the sink and publishes CommandRejected") {
  Fixture f;
  const CommandOrigin ai{CommandOrigin::Kind::Ai, "planner-run-7"};

  const auto error = f.bus.submit(Play{DeckId::A}, ai);  // nothing loaded on A

  REQUIRE(error.has_value());
  CHECK(error->code == CommandErrorCode::NoTrackLoaded);
  CHECK(f.store.snapshot()->revision == 0);
  CHECK(f.sink->count() == 0U);

  REQUIRE(f.received.size() == 1U);
  const auto* rejected = std::get_if<CommandRejected>(&f.received.front());
  REQUIRE(rejected != nullptr);
  CHECK(rejected->command == "PLAY");
  CHECK(rejected->error.code == CommandErrorCode::NoTrackLoaded);
  CHECK(rejected->origin.kind == CommandOrigin::Kind::Ai);
  CHECK(rejected->origin.sourceId == "planner-run-7");
}

TEST_CASE("commands are validated against the current state, in order") {
  Fixture f;

  REQUIRE_FALSE(f.bus.submit(LoadTrack{DeckId::B, TrackId{3}}, kUi));
  REQUIRE_FALSE(f.bus.submit(Play{DeckId::B}, kUi));

  const auto snapshot = f.store.snapshot();
  CHECK(snapshot->revision == 2);
  CHECK(snapshot->deck(DeckId::B).playing);
  CHECK(f.sink->names() == std::vector<std::string>{"LOAD_TRACK", "PLAY"});
}

TEST_CASE("every registered sink sees each accepted command in registration order") {
  Fixture f;
  auto second = std::make_shared<RecordingSink>();
  f.bus.addSink(second);
  f.bus.addSink(nullptr);  // ignored

  REQUIRE_FALSE(f.bus.submit(SetVolume{DeckId::A, 0.5F}, kUi));

  CHECK(f.sink->count() == 1U);
  CHECK(second->count() == 1U);
}

TEST_CASE("a sink added late is told the current state, and a removed sink hears nothing more") {
  Fixture f;
  REQUIRE_FALSE(f.bus.submit(SetGain{DeckId::A, 3.0F}, kUi));
  REQUIRE_FALSE(f.bus.submit(SetGain{DeckId::A, 4.0F}, kUi));

  auto late = std::make_shared<RecordingSink>();
  f.bus.addSink(late);
  CHECK(late->attachedRevision() == 2);  // synced to the state it missed

  REQUIRE_FALSE(f.bus.submit(SetGain{DeckId::A, 5.0F}, kUi));
  CHECK(late->count() == 1U);

  f.bus.removeSink(late);
  REQUIRE_FALSE(f.bus.submit(SetGain{DeckId::A, 6.0F}, kUi));
  CHECK(late->count() == 1U);
  CHECK(f.sink->count() == 4U);
}

TEST_CASE("snapshots taken before a command are not affected by it") {
  Fixture f;
  const auto before = f.store.snapshot();

  REQUIRE_FALSE(f.bus.submit(SetGain{DeckId::A, 6.0F}, kUi));

  CHECK(before->deck(DeckId::A).gainDb == 0.0F);
  CHECK(f.store.snapshot()->deck(DeckId::A).gainDb == 6.0F);
}

TEST_CASE("a sink that calls submit from inside onCommand gets a Reentrant error instead of deadlocking") {
  StateStore store;
  EventBus events;
  CommandBus bus{store, events};
  const auto sink = std::make_shared<ReentrantSink>(bus);
  bus.addSink(sink);

  REQUIRE_FALSE(bus.submit(SetVolume{DeckId::A, 0.25F}, kUi));

  CHECK(sink->calls == 1);
  REQUIRE(sink->result.has_value());
  CHECK(sink->result->code == CommandErrorCode::Reentrant);
  CHECK(store.snapshot()->revision == 1);  // the nested command was not applied
}

TEST_CASE("an event handler may submit a follow-up command because events are published after the lock") {
  Fixture f;
  bool followedUp = false;
  f.events.subscribe([&](const Event& event) {
    const auto* changed = std::get_if<StateChanged>(&event);
    if (changed != nullptr && changed->revision == 1 && !followedUp) {
      followedUp = true;
      REQUIRE_FALSE(f.bus.submit(SetVolume{DeckId::A, 0.5F}, kUi));
    }
  });

  REQUIRE_FALSE(f.bus.submit(SetGain{DeckId::A, 1.0F}, kUi));

  CHECK(f.store.snapshot()->revision == 2);
  CHECK(f.store.snapshot()->deck(DeckId::A).volume == 0.5F);
}

TEST_CASE("a throwing event handler cannot undo or hide an accepted command") {
  Fixture f;
  f.events.subscribe([](const Event&) { throw std::runtime_error("handler failed"); });

  std::optional<CommandError> error;
  CHECK_NOTHROW(error = f.bus.submit(SetGain{DeckId::A, 2.0F}, kUi));

  CHECK_FALSE(error.has_value());
  CHECK(f.store.snapshot()->deck(DeckId::A).gainDb == 2.0F);
  CHECK(f.sink->count() == 1U);
  CHECK(f.events.failedDeliveries() == 1U);
}

TEST_CASE("concurrent producers never lose, double-apply or overlap a command") {
  constexpr int kThreads = 4;
  constexpr int kPerThread = 250;

  StateStore store;
  EventBus events;
  CommandBus bus{store, events};
  const auto sink = std::make_shared<RecordingSink>();
  bus.addSink(sink);
  std::atomic<int> announced{0};
  std::atomic<int> rejections{0};
  std::atomic<int> hintViolations{0};
  events.subscribe([&](const Event& event) {
    announced.fetch_add(1);
    // StateChanged is an invalidation hint: reading the snapshot inside a handler never yields an older state.
    if (const auto* changed = std::get_if<StateChanged>(&event)) {
      if (store.snapshot()->revision < changed->revision) {
        hintViolations.fetch_add(1);
      }
    }
  });

  zyron::test::ThreadGroup producers;
  for (int t = 0; t < kThreads; ++t) {
    producers.spawn([&, t] {
      const CommandOrigin origin{CommandOrigin::Kind::Midi, "thread-" + std::to_string(t)};
      for (int i = 0; i < kPerThread; ++i) {
        const float volume = static_cast<float>(i % 101) / 100.0F;
        // Catch2 assertions are not used off the main thread; failures are counted and checked after join.
        if (bus.submit(SetVolume{DeckId::A, volume}, origin).has_value()) {
          rejections.fetch_add(1);
        }
      }
    });
  }
  producers.joinAll();

  constexpr int kTotal = kThreads * kPerThread;
  CHECK(rejections.load() == 0);
  CHECK(store.snapshot()->revision == static_cast<std::uint64_t>(kTotal));
  CHECK(sink->count() == static_cast<std::size_t>(kTotal));
  CHECK(announced.load() == kTotal);
  CHECK_FALSE(sink->overlapped());
  CHECK(hintViolations.load() == 0);
}

TEST_CASE("racing Play against UnloadTrack never exposes a playing deck without a track") {
  StateStore store;
  EventBus events;
  CommandBus bus{store, events};
  REQUIRE_FALSE(bus.submit(LoadTrack{DeckId::A, TrackId{1}}, kUi));

  std::atomic<bool> stop{false};
  std::atomic<int> inconsistent{0};
  std::atomic<std::uint64_t> reads{0};

  zyron::test::ThreadGroup reader;  // declared first so it is joined last
  struct StopReader {
    std::atomic<bool>& stop;
    ~StopReader() { stop.store(true); }
  } stopReader{stop};  // lets the reader exit even if the test body throws

  reader.spawn([&] {
    while (!stop.load()) {
      const auto snap = store.snapshot();
      const DeckState& deck = snap->deck(DeckId::A);
      if (deck.playing && !deck.hasTrack()) {
        inconsistent.fetch_add(1);
      }
      reads.fetch_add(1);
    }
  });

  zyron::test::ThreadGroup writers;
  writers.spawn([&] {
    for (int i = 0; i < 300; ++i) {
      (void)bus.submit(LoadTrack{DeckId::A, TrackId{1}}, kUi);
      (void)bus.submit(Play{DeckId::A}, kUi);
    }
  });
  writers.spawn([&] {
    for (int i = 0; i < 300; ++i) {
      (void)bus.submit(UnloadTrack{DeckId::A}, kUi);
    }
  });
  writers.joinAll();
  stop.store(true);
  reader.joinAll();

  CHECK(reads.load() > 0U);
  CHECK(inconsistent.load() == 0);
}

TEST_CASE("Stem commands update AppState and are validated") {
  Fixture f;

  // SetStemVolume
  auto err = f.bus.submit(SetStemVolume{DeckId::A, StemKind::Vocals, 0.75F}, kUi);
  CHECK_FALSE(err.has_value());
  auto snap = f.store.snapshot();
  CHECK(snap->deck(DeckId::A).stems.at(index(StemKind::Vocals)).volume == 0.75F);

  // Rejects out of range volume
  err = f.bus.submit(SetStemVolume{DeckId::A, StemKind::Vocals, 1.5F}, kUi);
  REQUIRE(err.has_value());
  CHECK(err->code == CommandErrorCode::OutOfRange);

  // SetStemMute
  err = f.bus.submit(SetStemMute{DeckId::A, StemKind::Drums, true}, kUi);
  CHECK_FALSE(err.has_value());
  snap = f.store.snapshot();
  CHECK(snap->deck(DeckId::A).stems.at(index(StemKind::Drums)).muted);

  // SetStemSolo
  err = f.bus.submit(SetStemSolo{DeckId::A, StemKind::Bass, true}, kUi);
  CHECK_FALSE(err.has_value());
  snap = f.store.snapshot();
  CHECK(snap->deck(DeckId::A).stems.at(index(StemKind::Bass)).solo);

  // SetStemCue
  err = f.bus.submit(SetStemCue{DeckId::A, StemKind::Other, true}, kUi);
  CHECK_FALSE(err.has_value());
  snap = f.store.snapshot();
  CHECK(snap->deck(DeckId::A).stems.at(index(StemKind::Other)).cue);
}
