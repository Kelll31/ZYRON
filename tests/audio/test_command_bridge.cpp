// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "Audio/Bridge/CommandBridge.hpp"
#include "support/AllocationGuard.hpp"

using namespace zyron::audio;
using namespace zyron::core;
using namespace zyron::test;
using Catch::Matchers::WithinAbs;

TEST_CASE("CommandBridge operations are strictly realtime safe", "[audio][bridge][rt]") {
  CommandBridge bridge;

  {
    ScopedRealtimeGuard rtGuard;

    // Push raw message
    CHECK(bridge.pushMessage(RtMessage::makePlay(DeckId::A)));

    // Pop message
    RtMessage popped;
    CHECK(bridge.popMessage(popped));
    CHECK(popped.type == RtMessageType::DeckPlay);
    CHECK(popped.deck == DeckId::A);

    // Push Command
    CHECK(bridge.pushCommand(SetGain{DeckId::B, 2.5F}));
    CHECK(bridge.popMessage(popped));
    CHECK(popped.type == RtMessageType::DeckGain);
    CHECK(popped.deck == DeckId::B);
    CHECK(popped.data.gainDb == 2.5F);

    // Publish telemetry
    AudioTelemetry telem{};
    telem.masterPeakLeft = 0.75F;
    telem.decks[index(DeckId::A)].isPlaying = true;
    bridge.publishTelemetry(telem);

    // Read telemetry
    AudioTelemetry readBack{};
    CHECK(bridge.readTelemetry(readBack));
    CHECK(readBack.masterPeakLeft == 0.75F);
    CHECK(readBack.decks[index(DeckId::A)].isPlaying);
  }
}

TEST_CASE("CommandBridge translates Core commands accurately", "[audio][bridge][commands]") {
  CommandBridge bridge;
  RtMessage msg;

  SECTION("Playback commands") {
    bridge.pushCommand(Play{DeckId::A});
    CHECK(bridge.popMessage(msg));
    CHECK(msg.type == RtMessageType::DeckPlay);
    CHECK(msg.deck == DeckId::A);

    bridge.pushCommand(Pause{DeckId::B});
    CHECK(bridge.popMessage(msg));
    CHECK(msg.type == RtMessageType::DeckPause);
    CHECK(msg.deck == DeckId::B);

    bridge.pushCommand(Cue{DeckId::C});
    CHECK(bridge.popMessage(msg));
    CHECK(msg.type == RtMessageType::DeckCue);
    CHECK(msg.deck == DeckId::C);
  }

  SECTION("Channel strip commands") {
    bridge.pushCommand(SetGain{DeckId::A, -3.5F});
    CHECK(bridge.popMessage(msg));
    CHECK(msg.type == RtMessageType::DeckGain);
    CHECK(msg.deck == DeckId::A);
    CHECK(msg.data.gainDb == -3.5F);

    bridge.pushCommand(SetVolume{DeckId::B, 0.85F});
    CHECK(bridge.popMessage(msg));
    CHECK(msg.type == RtMessageType::DeckVolume);
    CHECK(msg.deck == DeckId::B);
    CHECK(msg.data.volumeLinear == 0.85F);

    bridge.pushCommand(SetEq{DeckId::D, EqBand::Mid, 4.0F});
    CHECK(bridge.popMessage(msg));
    CHECK(msg.type == RtMessageType::DeckEq);
    CHECK(msg.deck == DeckId::D);
    CHECK(msg.data.eq.band == EqBand::Mid);
    CHECK(msg.data.eq.gainDb == 4.0F);
  }

  SECTION("Test tone command") {
    bridge.pushCommand(SetTestTone{true, 1000.0F, -12.0F});
    CHECK(bridge.popMessage(msg));
    CHECK(msg.type == RtMessageType::TestTone);
    CHECK(msg.data.testTone.enabled);
    CHECK(msg.data.testTone.frequencyHz == 1000.0F);
    CHECK(msg.data.testTone.levelDb == -12.0F);
  }
}

TEST_CASE("CommandBridge FIFO order and empty queue behavior", "[audio][bridge][fifo]") {
  CommandBridge bridge;

  RtMessage popped;
  CHECK_FALSE(bridge.popMessage(popped));

  bridge.pushMessage(RtMessage::makePlay(DeckId::A));
  bridge.pushMessage(RtMessage::makeGain(DeckId::A, -6.0F));
  bridge.pushMessage(RtMessage::makePause(DeckId::A));

  CHECK(bridge.popMessage(popped));
  CHECK(popped.type == RtMessageType::DeckPlay);

  CHECK(bridge.popMessage(popped));
  CHECK(popped.type == RtMessageType::DeckGain);
  CHECK(popped.data.gainDb == -6.0F);

  CHECK(bridge.popMessage(popped));
  CHECK(popped.type == RtMessageType::DeckPause);

  CHECK_FALSE(bridge.popMessage(popped));
}

TEST_CASE("CommandBridge queue overflow drops newest and increments drop counter", "[audio][bridge][overflow]") {
  CommandBridge bridge;

  // Fill the queue completely (1024 capacity)
  for (std::size_t i = 0; i < CommandBridge::kQueueCapacity; ++i) {
    const bool pushed = bridge.pushMessage(RtMessage::makeCrossfader(static_cast<float>(i)));
    CHECK(pushed);
  }

  CHECK(bridge.droppedMessagesCount() == 0);

  // Pushing when full should fail and count drops
  constexpr int kOverflowCount = 15;
  for (int i = 0; i < kOverflowCount; ++i) {
    const bool pushed = bridge.pushMessage(RtMessage::makeMasterGain(0.0F));
    CHECK_FALSE(pushed);
  }

  CHECK(bridge.droppedMessagesCount() == kOverflowCount);

  // Popping one element creates space again
  RtMessage popped;
  CHECK(bridge.popMessage(popped));
  CHECK(popped.type == RtMessageType::MixerCrossfader);

  // Now push should succeed
  CHECK(bridge.pushMessage(RtMessage::makeMasterGain(0.0F)));
}

TEST_CASE("CommandBridge telemetry triple buffering", "[audio][bridge][telemetry]") {
  CommandBridge bridge;

  AudioTelemetry initial{};
  CHECK_FALSE(bridge.readTelemetry(initial));

  // Audio thread publishes snapshot #1
  AudioTelemetry snap1{};
  snap1.masterPeakLeft = 0.5F;
  snap1.decks[0].playheadSample = 1000;
  bridge.publishTelemetry(snap1);

  // UI thread reads snapshot #1
  AudioTelemetry read1{};
  CHECK(bridge.readTelemetry(read1));
  CHECK(read1.masterPeakLeft == 0.5F);
  CHECK(read1.decks[0].playheadSample == 1000);

  // Second read without new publish returns false (no new data) but preserves content
  AudioTelemetry readAgain{};
  CHECK_FALSE(bridge.readTelemetry(readAgain));
  CHECK(readAgain.masterPeakLeft == 0.5F);

  // Audio thread publishes multiple snapshots rapidly
  for (int i = 0; i < 10; ++i) {
    AudioTelemetry fastSnap{};
    fastSnap.masterPeakLeft = static_cast<float>(i) * 0.1F;
    fastSnap.decks[0].playheadSample = 2000 + i;
    bridge.publishTelemetry(fastSnap);
  }

  // Reader receives the latest published snapshot without tearing
  AudioTelemetry latest{};
  CHECK(bridge.readTelemetry(latest));
  CHECK_THAT(latest.masterPeakLeft, WithinAbs(0.9F, 1e-4F));
  CHECK(latest.decks[0].playheadSample == 2009);
}

TEST_CASE("CommandBridge multi-threaded producer-consumer concurrency", "[audio][bridge][threading]") {
  CommandBridge bridge;
  constexpr int kTotalMessages = 20000;

  std::atomic<bool> producerDone{false};
  std::atomic<int> receivedCount{0};

  std::thread consumer([&bridge, &producerDone, &receivedCount]() {
    RtMessage msg;
    while (!producerDone.load(std::memory_order_acquire)) {
      while (bridge.popMessage(msg)) {
        receivedCount.fetch_add(1, std::memory_order_relaxed);
      }
      std::this_thread::yield();
    }
    // Drain any remaining messages after producer has finished
    while (bridge.popMessage(msg)) {
      receivedCount.fetch_add(1, std::memory_order_relaxed);
    }
  });

  std::thread producer([&bridge, &producerDone]() {
    for (int i = 0; i < kTotalMessages; ++i) {
      while (bridge.queueSize() >= CommandBridge::kQueueCapacity - 1) {
        std::this_thread::yield();
      }
      const bool pushed = bridge.pushMessage(RtMessage::makeSeek(DeckId::A, i));
      CHECK(pushed);
    }
    producerDone.store(true, std::memory_order_release);
  });

  producer.join();
  consumer.join();

  CHECK(receivedCount.load() == kTotalMessages);
  CHECK(bridge.droppedMessagesCount() == 0);
}
