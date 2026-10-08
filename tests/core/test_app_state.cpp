// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>

#include "Core/State/AppState.hpp"
#include "support/ThreadGroup.hpp"

using namespace zyron::core;

TEST_CASE("a default AppState has four empty, stopped decks at neutral settings") {
  const AppState state;

  CHECK(state.revision == 0);
  for (std::size_t i = 0; i < kDeckCount; ++i) {
    const DeckState& deck = state.decks[i];
    CHECK_FALSE(deck.hasTrack());
    CHECK_FALSE(deck.playing);
    CHECK(deck.gainDb == 0.0F);
    CHECK(deck.volume == 1.0F);
    for (const float eq : deck.eqDb) {
      CHECK(eq == 0.0F);
    }
    for (const auto& stem : deck.stems) {
      CHECK(stem.volume == 1.0F);
      CHECK_FALSE(stem.muted);
      CHECK_FALSE(stem.solo);
      CHECK_FALSE(stem.cue);
    }
  }
}

TEST_CASE("deck() addresses decks by id and rejects ids out of range") {
  AppState state;
  state.decks[index(DeckId::C)].track = TrackId{5};

  CHECK(state.deck(DeckId::C).track == TrackId{5});
  CHECK_FALSE(state.deck(DeckId::A).hasTrack());
  CHECK_THROWS_AS(state.deck(static_cast<DeckId>(kDeckCount)), std::out_of_range);
}

TEST_CASE("TrackId zero means no track") {
  CHECK_FALSE(TrackId{}.isValid());
  CHECK_FALSE(TrackId{-1}.isValid());
  CHECK(TrackId{1}.isValid());
}

TEST_CASE("StateStore starts at revision 0 and publishes immutable snapshots") {
  StateStore store;
  const auto first = store.snapshot();
  REQUIRE(first != nullptr);
  CHECK(first->revision == 0);

  AppState next = *first;
  next.revision = 1;
  next.decks[index(DeckId::A)].playing = true;
  REQUIRE(store.publishIfRevision(0, next));

  const auto second = store.snapshot();
  CHECK(second->revision == 1);
  CHECK(second->deck(DeckId::A).playing);

  // A reader holding the old snapshot keeps seeing the old data.
  CHECK(first->revision == 0);
  CHECK_FALSE(first->deck(DeckId::A).playing);
}

TEST_CASE("publishIfRevision only succeeds from the revision the caller started from") {
  StateStore store;
  AppState one;
  one.revision = 1;
  one.decks[index(DeckId::A)].gainDb = 1.0F;
  REQUIRE(store.publishIfRevision(0, one));

  // A second writer that still believes the store is at revision 0 must be refused and change nothing.
  AppState stale;
  stale.revision = 1;
  stale.decks[index(DeckId::A)].gainDb = 99.0F;
  CHECK_FALSE(store.publishIfRevision(0, stale));

  CHECK(store.snapshot()->revision == 1);
  CHECK(store.snapshot()->deck(DeckId::A).gainDb == 1.0F);

  AppState two;
  two.revision = 2;
  CHECK(store.publishIfRevision(1, two));
  CHECK(store.snapshot()->revision == 2);
}

TEST_CASE("readers never see a torn or backwards-moving state while a writer publishes") {
  constexpr std::uint64_t kPublishes = 2000;
  constexpr int kReaders = 3;
  constexpr std::uint64_t kMinReadsPerReader = 50;

  StateStore store;
  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  std::atomic<int> violations{0};
  std::atomic<int> starvedReaders{0};

  zyron::test::ThreadGroup readers;  // declared first: destroyed (joined) last
  struct StopReaders {
    std::atomic<bool>& done;
    ~StopReaders() { done.store(true, std::memory_order_release); }
  } stopReaders{done};  // sets `done` first if the test body throws, so the readers can exit

  for (int r = 0; r < kReaders; ++r) {
    readers.spawn([&] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      std::uint64_t last = 0;
      std::uint64_t reads = 0;
      while (!done.load(std::memory_order_acquire) || reads < kMinReadsPerReader) {
        const auto snap = store.snapshot();
        // The writer keeps revision and deck A's gain in lock-step; a mismatch would be a torn read.
        if (snap->revision < last || snap->deck(DeckId::A).gainDb != static_cast<float>(snap->revision)) {
          violations.fetch_add(1);
        }
        last = snap->revision;
        ++reads;
        std::this_thread::yield();
      }
      if (reads < kMinReadsPerReader) {
        starvedReaders.fetch_add(1);
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (std::uint64_t i = 1; i <= kPublishes; ++i) {
    AppState next;
    next.revision = i;
    next.decks[index(DeckId::A)].gainDb = static_cast<float>(i);
    REQUIRE(store.publishIfRevision(i - 1, next));
  }
  done.store(true, std::memory_order_release);
  readers.joinAll();

  CHECK(violations.load() == 0);
  CHECK(starvedReaders.load() == 0);
  CHECK(store.snapshot()->revision == kPublishes);
}
