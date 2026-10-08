// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using zyron::audio::DeckPlayer;
using zyron::audio::TrackBuffer;
using namespace zyron::test;

namespace {

constexpr double kSampleRate = 48000.0;

std::shared_ptr<TrackBuffer> createSineTrack(double durationSec, double freqHz, float amplitude = 1.0F) {
  const auto samples = generateSine(kSampleRate, freqHz, durationSec, amplitude);
  const auto frames = static_cast<std::int64_t>(samples.size());
  auto track = std::make_shared<TrackBuffer>(2, frames, kSampleRate);
  std::copy(samples.begin(), samples.end(), track->channelData(0));
  std::copy(samples.begin(), samples.end(), track->channelData(1));
  return track;
}

}  // namespace

TEST_CASE("TrackBuffer storage and sample access") {
  TrackBuffer buffer(2, 4800, kSampleRate);
  REQUIRE(buffer.numChannels() == 2);
  REQUIRE(buffer.numFrames() == 4800);
  REQUIRE(buffer.sampleRate() == kSampleRate);
  REQUIRE_THAT(buffer.durationSec(), Catch::Matchers::WithinAbs(0.1, 0.001));

  buffer.channelData(0)[100] = 0.42F;
  buffer.channelData(1)[100] = -0.42F;

  CHECK(buffer.sampleAt(0, 100) == 0.42F);
  CHECK(buffer.sampleAt(1, 100) == -0.42F);

  // Out of bounds queries return silence
  CHECK(buffer.sampleAt(-1, 100) == 0.0F);
  CHECK(buffer.sampleAt(2, 100) == 0.0F);
  CHECK(buffer.sampleAt(0, -1) == 0.0F);
  CHECK(buffer.sampleAt(0, 4800) == 0.0F);
}

TEST_CASE("DeckPlayer basic lifecycle and playback controls") {
  DeckPlayer deck;
  deck.prepare(kSampleRate);

  CHECK_FALSE(deck.hasTrack());
  CHECK_FALSE(deck.isPlaying());
  CHECK(deck.durationSec() == 0.0);

  auto track = createSineTrack(1.0, 1000.0, 0.8F);
  deck.loadTrack(track);

  CHECK(deck.hasTrack());
  CHECK_FALSE(deck.isPlaying());
  CHECK_THAT(deck.durationSec(), Catch::Matchers::WithinAbs(1.0, 0.001));
  CHECK(deck.currentFrame() == 0);

  SECTION("play and pause change playback status and produce audio") {
    deck.play();
    CHECK(deck.isPlaying());

    std::vector<float> left(480, 0.0F);
    std::vector<float> right(480, 0.0F);
    float* channels[2] = {left.data(), right.data()};

    // Render several blocks so fade-in completes
    for (int i = 0; i < 4; ++i) {
      deck.render(channels, 2, 480);
    }

    CHECK(deck.currentFrame() > 0);
    CHECK(maxAbsolute(left) > 0.1F);
    CHECK(maxAbsolute(right) > 0.1F);

    deck.pause();
    // After sufficient renders for fade-out, it settles to paused
    for (int i = 0; i < 10; ++i) {
      deck.render(channels, 2, 480);
    }
    CHECK_FALSE(deck.isPlaying());
  }

  SECTION("cue returns to cue position when playing") {
    deck.play();
    std::vector<float> out(480, 0.0F);
    float* channels[1] = {out.data()};
    deck.render(channels, 1, 480);

    CHECK(deck.currentFrame() == 480);
    deck.cue();
    CHECK(deck.currentFrame() == 0);
    CHECK_FALSE(deck.isPlaying());
  }

  SECTION("cue sets cue position when paused") {
    deck.seek(1200);
    deck.cue();
    CHECK(deck.cueFrame() == 1200);
  }

  SECTION("seeking updates playhead within bounds") {
    deck.seek(2400);
    CHECK(deck.currentFrame() == 2400);

    deck.seek(-100);
    CHECK(deck.currentFrame() == 0);

    deck.seek(999999);
    CHECK(deck.currentFrame() == track->numFrames());

    deck.seekSeconds(0.5);
    CHECK(deck.currentFrame() == 24000);
  }

  SECTION("playback speed alters advance rate") {
    deck.setPlaybackSpeed(2.0);
    CHECK(deck.playbackSpeed() == 2.0);

    deck.play();
    std::vector<float> out(100, 0.0F);
    float* channels[1] = {out.data()};
    deck.render(channels, 1, 100);

    // At 2x speed, 100 output samples consumes 200 input frames
    CHECK(deck.currentFrame() == 200);
  }

  SECTION("unloading track resets player") {
    deck.unloadTrack();
    CHECK_FALSE(deck.hasTrack());
    CHECK_FALSE(deck.isPlaying());
  }
}

TEST_CASE("DeckPlayer is realtime safe (zero allocations during render)") {
  DeckPlayer deck;
  deck.prepare(kSampleRate);
  auto track = createSineTrack(2.0, 440.0, 0.5F);
  deck.loadTrack(track);
  deck.play();

  std::vector<float> left(512, 0.0F);
  std::vector<float> right(512, 0.0F);
  float* channels[2] = {left.data(), right.data()};

  ScopedRealtimeGuard guard;
  deck.render(channels, 2, 512);
  REQUIRE_FALSE(guard.hasViolations());
  REQUIRE(guard.allocationCount() == 0);
  REQUIRE(guard.deallocationCount() == 0);
}
