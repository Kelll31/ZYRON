// SPDX-License-Identifier: AGPL-3.0-only
// CommandBridge::translateCommand for the playback commands (Seek, SetPlaybackSpeed, SetLoop, ...).
#include <catch2/catch_test_macros.hpp>

#include "Audio/Bridge/CommandBridge.hpp"

using namespace zyron::audio;
using namespace zyron::core;

TEST_CASE("translateCommand maps Seek to a seconds-based seek message", "[audio][bridge][translate]") {
  const auto msg = CommandBridge::translateCommand(Seek{DeckId::C, 12.5});
  REQUIRE(msg.has_value());
  CHECK(msg->type == RtMessageType::DeckSeekSeconds);
  CHECK(msg->deck == DeckId::C);
  CHECK(msg->data.seekSeconds == 12.5);
}

TEST_CASE("translateCommand maps SetPlaybackSpeed to a speed ratio message", "[audio][bridge][translate]") {
  const auto msg = CommandBridge::translateCommand(SetPlaybackSpeed{DeckId::B, 1.08});
  REQUIRE(msg.has_value());
  CHECK(msg->type == RtMessageType::DeckPlaybackSpeed);
  CHECK(msg->deck == DeckId::B);
  CHECK(msg->data.speedRatio == 1.08);
}

TEST_CASE("translateCommand maps SetLoop with region and activity", "[audio][bridge][translate]") {
  const auto on = CommandBridge::translateCommand(SetLoop{DeckId::D, 4.0, 8.0, true});
  REQUIRE(on.has_value());
  CHECK(on->type == RtMessageType::DeckLoop);
  CHECK(on->deck == DeckId::D);
  CHECK(on->data.loop.startSeconds == 4.0);
  CHECK(on->data.loop.endSeconds == 8.0);
  CHECK(on->data.loop.active);

  const auto off = CommandBridge::translateCommand(SetLoop{DeckId::D, 4.0, 8.0, false});
  REQUIRE(off.has_value());
  CHECK_FALSE(off->data.loop.active);
}

TEST_CASE("translateCommand maps UnloadTrack to a pause", "[audio][bridge][translate]") {
  const auto msg = CommandBridge::translateCommand(UnloadTrack{DeckId::A});
  REQUIRE(msg.has_value());
  CHECK(msg->type == RtMessageType::DeckPause);
  CHECK(msg->deck == DeckId::A);
}

TEST_CASE("translateCommand returns nothing for commands handled off the audio thread", "[audio][bridge][translate]") {
  CHECK_FALSE(CommandBridge::translateCommand(SetRecording{true}).has_value());
  CHECK_FALSE(CommandBridge::translateCommand(Sync{DeckId::A}).has_value());
  CHECK_FALSE(CommandBridge::translateCommand(SeparateStems{DeckId::A}).has_value());
  CHECK_FALSE(CommandBridge::translateCommand(LoadTrack{DeckId::A, TrackId{1}}).has_value());
  CHECK_FALSE(CommandBridge::translateCommand(SetAudioOutput{}).has_value());
}

TEST_CASE("pushCommand succeeds without enqueueing for non-realtime commands", "[audio][bridge][translate]") {
  CommandBridge bridge;
  CHECK(bridge.pushCommand(Sync{DeckId::A}));
  CHECK(bridge.pushCommand(SetRecording{true}));
  CHECK(bridge.queueSize() == 0);

  CHECK(bridge.pushCommand(Seek{DeckId::A, 3.0}));
  REQUIRE(bridge.queueSize() == 1);
  RtMessage msg;
  REQUIRE(bridge.popMessage(msg));
  CHECK(msg.type == RtMessageType::DeckSeekSeconds);
}

TEST_CASE("pushCommand reports a full queue and counts the drop", "[audio][bridge][translate]") {
  CommandBridge bridge;
  for (std::size_t i = 0; i < CommandBridge::kQueueCapacity; ++i) {
    REQUIRE(bridge.pushCommand(Play{DeckId::A}));
  }
  CHECK_FALSE(bridge.pushCommand(Play{DeckId::A}));
  CHECK(bridge.droppedMessagesCount() == 1);
}
