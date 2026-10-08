// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

#include "Audio/Effects/DelayEffect.hpp"
#include "Audio/Effects/ReverbEffect.hpp"
#include "Audio/Engine/AudioGraph.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron;
using namespace zyron::audio;
using namespace zyron::test;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = std::numbers::pi;

std::shared_ptr<TrackBuffer> makeSineTrack(float freqHz, double durationSec, double sampleRate) {
  const auto totalFrames = static_cast<std::int64_t>(std::round(durationSec * sampleRate));
  auto tb = std::make_shared<TrackBuffer>(2, totalFrames, sampleRate);
  float* left = tb->channelData(0);
  float* right = tb->channelData(1);

  const double phaseInc = 2.0 * kPi * static_cast<double>(freqHz) / sampleRate;
  double phase = 0.0;

  for (std::int64_t i = 0; i < totalFrames; ++i) {
    const float val = static_cast<float>(std::sin(phase));
    left[i] = val;
    right[i] = val;
    phase += phaseInc;
    if (phase >= 2.0 * kPi) {
      phase -= 2.0 * kPi;
    }
  }
  return tb;
}

}  // namespace

TEST_CASE("AudioGraph: 4 decks simultaneous playback and rendering", "[audio][graph][4decks]") {
  AudioGraph graph;
  graph.prepare(kRate);

  auto trackA = makeSineTrack(440.0F, 1.0, kRate);
  auto trackB = makeSineTrack(880.0F, 1.0, kRate);
  auto trackC = makeSineTrack(1320.0F, 1.0, kRate);
  auto trackD = makeSineTrack(1760.0F, 1.0, kRate);

  graph.deck(core::DeckId::A).loadTrack(trackA);
  graph.deck(core::DeckId::B).loadTrack(trackB);
  graph.deck(core::DeckId::C).loadTrack(trackC);
  graph.deck(core::DeckId::D).loadTrack(trackD);

  graph.deck(core::DeckId::A).play();
  graph.deck(core::DeckId::B).play();
  graph.deck(core::DeckId::C).play();
  graph.deck(core::DeckId::D).play();

  // Crossfader in center (0.0): all 4 decks contribute to master
  graph.mixer().setCrossfader(0.0F);
  graph.reset();

  constexpr int kBlockSize = 256;
  std::vector<float> masterL(kBlockSize, 0.0F);
  std::vector<float> masterR(kBlockSize, 0.0F);
  float* outPtrs[2] = {masterL.data(), masterR.data()};

  graph.render(outPtrs, 2, kBlockSize);

  float maxPeak = 0.0F;
  for (int i = 0; i < kBlockSize; ++i) {
    maxPeak = std::max(maxPeak, std::abs(masterL[static_cast<std::size_t>(i)]));
  }
  CHECK(maxPeak > 0.1F);
}

TEST_CASE("AudioGraph: crossfader assign A,C->L and B,D->R default and configurable", "[audio][graph][crossfader]") {
  AudioGraph graph;
  graph.prepare(kRate);
  graph.mixer().masterLimiter().setLookaheadMs(0.0F);
  graph.mixer().masterLimiter().setCeilingDb(0.0F);

  auto trackA = makeSineTrack(440.0F, 1.0, kRate);
  auto trackB = makeSineTrack(880.0F, 1.0, kRate);
  auto trackC = makeSineTrack(1320.0F, 1.0, kRate);
  auto trackD = makeSineTrack(1760.0F, 1.0, kRate);

  graph.deck(core::DeckId::A).loadTrack(trackA);
  graph.deck(core::DeckId::B).loadTrack(trackB);
  graph.deck(core::DeckId::C).loadTrack(trackC);
  graph.deck(core::DeckId::D).loadTrack(trackD);

  // Check default assignments (§20)
  CHECK(graph.mixer().channelAssign(0) == core::CrossfaderAssign::Left);
  CHECK(graph.mixer().channelAssign(1) == core::CrossfaderAssign::Right);
  CHECK(graph.mixer().channelAssign(2) == core::CrossfaderAssign::Left);
  CHECK(graph.mixer().channelAssign(3) == core::CrossfaderAssign::Right);

  constexpr int kBlockSize = 512;
  std::vector<float> outL(kBlockSize, 0.0F);
  std::vector<float> outR(kBlockSize, 0.0F);
  float* outPtrs[2] = {outL.data(), outR.data()};

  SECTION("Full Left crossfader (-1.0): Decks A and C heard, Decks B and D muted") {
    // Only B and D playing (assigned to Right)
    graph.deck(core::DeckId::B).play();
    graph.deck(core::DeckId::D).play();

    graph.mixer().setCrossfader(-1.0F);
    graph.reset();

    for (int b = 0; b < 4; ++b) {
      graph.render(outPtrs, 2, kBlockSize);
    }

    float bleed = 0.0F;
    for (int i = 0; i < kBlockSize; ++i) {
      bleed = std::max(bleed, std::abs(outL[static_cast<std::size_t>(i)]));
    }
    CHECK(bleed < 1e-4F);

    // Now start A and C (assigned to Left) -> should immediately be heard
    graph.deck(core::DeckId::A).play();
    graph.deck(core::DeckId::C).play();
    for (int b = 0; b < 4; ++b) {
      graph.render(outPtrs, 2, kBlockSize);
    }
    float heard = 0.0F;
    for (int i = 0; i < kBlockSize; ++i) {
      heard = std::max(heard, std::abs(outL[static_cast<std::size_t>(i)]));
    }
    CHECK(heard > 0.1F);
  }

  SECTION("Full Right crossfader (+1.0): Decks B and D heard, Decks A and C muted") {
    // Only A and C playing (assigned to Left)
    graph.deck(core::DeckId::A).play();
    graph.deck(core::DeckId::C).play();

    graph.mixer().setCrossfader(1.0F);
    graph.reset();

    for (int b = 0; b < 4; ++b) {
      graph.render(outPtrs, 2, kBlockSize);
    }

    float bleed = 0.0F;
    for (int i = 0; i < kBlockSize; ++i) {
      bleed = std::max(bleed, std::abs(outL[static_cast<std::size_t>(i)]));
    }
    CHECK(bleed < 1e-4F);

    // Now start B and D (assigned to Right) -> should immediately be heard
    graph.deck(core::DeckId::B).play();
    graph.deck(core::DeckId::D).play();
    for (int b = 0; b < 4; ++b) {
      graph.render(outPtrs, 2, kBlockSize);
    }
    float heard = 0.0F;
    for (int i = 0; i < kBlockSize; ++i) {
      heard = std::max(heard, std::abs(outL[static_cast<std::size_t>(i)]));
    }
    CHECK(heard > 0.1F);
  }

  SECTION("Reconfigurable crossfader assign: Deck C assigned to Thru") {
    graph.deck(core::DeckId::C).play();
    graph.mixer().setChannelAssign(2, core::CrossfaderAssign::Thru);
    graph.mixer().setCrossfader(1.0F);  // Hard Right
    graph.reset();

    for (int b = 0; b < 4; ++b) {
      graph.render(outPtrs, 2, kBlockSize);
    }

    float maxVal = 0.0F;
    for (int i = 0; i < kBlockSize; ++i) {
      maxVal = std::max(maxVal, std::abs(outL[static_cast<std::size_t>(i)]));
    }
    // Deck C (Thru) bypasses crossfader and is heard on master!
    CHECK(maxVal > 0.1F);
  }
}

TEST_CASE("AudioGraph: 4-deck Cue bus routing", "[audio][graph][cue]") {
  AudioGraph graph;
  graph.prepare(kRate);

  auto trackC = makeSineTrack(1000.0F, 1.0, kRate);
  auto trackD = makeSineTrack(2000.0F, 1.0, kRate);

  graph.deck(core::DeckId::C).loadTrack(trackC);
  graph.deck(core::DeckId::D).loadTrack(trackD);
  graph.deck(core::DeckId::C).play();
  graph.deck(core::DeckId::D).play();

  // MultiChannel mode: Master on ch 0,1; Headphone Cue on ch 2,3
  graph.cueRouter().setMode(HeadphoneRoutingMode::MultiChannel);
  graph.cueRouter().setHeadphoneMix(0.0F);  // 100% Cue signal

  // Cue Deck C only
  graph.mixer().setCue(2, true);
  graph.mixer().setCue(3, false);

  constexpr int kBlockSize = 256;
  std::vector<float> multiOut[4];
  for (auto& ch : multiOut) {
    ch.resize(kBlockSize, 0.0F);
  }
  float* outPtrs[4] = {multiOut[0].data(), multiOut[1].data(), multiOut[2].data(), multiOut[3].data()};

  graph.render(outPtrs, 4, kBlockSize);

  float maxCue = 0.0F;
  for (int i = 0; i < kBlockSize; ++i) {
    maxCue = std::max(maxCue, std::abs(multiOut[2][static_cast<std::size_t>(i)]));
  }
  CHECK(maxCue > 0.05F);
}

TEST_CASE("AudioGraph is strictly realtime safe (zero heap allocations)", "[audio][graph][rt]") {
  AudioGraph graph;
  graph.prepare(kRate);

  auto track = makeSineTrack(440.0F, 1.0, kRate);
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    graph.deck(i).loadTrack(track);
    graph.deck(i).play();
  }

  constexpr int kBlockSize = 512;
  std::vector<float> multiOut[4];
  for (auto& ch : multiOut) {
    ch.resize(kBlockSize, 0.0F);
  }
  float* outPtrs[4] = {multiOut[0].data(), multiOut[1].data(), multiOut[2].data(), multiOut[3].data()};

  // Push commands into bridge before RT loop
  graph.bridge().pushCommand(core::Play{core::DeckId::A});
  graph.bridge().pushCommand(core::SetGain{core::DeckId::B, 2.0F});
  graph.bridge().pushCommand(core::SetVolume{core::DeckId::C, 0.8F});
  graph.bridge().pushCommand(core::SetCrossfader{0.25F});
  graph.bridge().pushCommand(core::SetCrossfaderAssign{core::DeckId::C, core::CrossfaderAssign::Thru});
  graph.bridge().pushCommand(core::SetMasterGain{0.0F});

  {
    ScopedRealtimeGuard rtGuard;
    for (int b = 0; b < 8; ++b) {
      graph.render(outPtrs, 4, kBlockSize);
    }
  }

  // Telemetry published
  AudioTelemetry telem{};
  REQUIRE(graph.bridge().readTelemetry(telem));
  CHECK(telem.callbackCount >= 8);
  CHECK(telem.decks[0].isPlaying);
  CHECK(telem.decks[2].isPlaying);
}

TEST_CASE("CPU budget and soak test: 4 decks + EQ + Filter + FX at 64/128/256 frames (P4-04)", "[audio][graph][cpu][soak]") {
  const auto bufferSizes = {64, 128, 256};
  for (int blockSize : bufferSizes) {
    DYNAMIC_SECTION("Buffer size " << blockSize << " frames") {
      AudioGraph graph;
      graph.prepare(kRate);

      // Load 4 tracks
      auto trackA = makeSineTrack(440.0F, 5.0, kRate);
      auto trackB = makeSineTrack(880.0F, 5.0, kRate);
      auto trackC = makeSineTrack(1320.0F, 5.0, kRate);
      auto trackD = makeSineTrack(1760.0F, 5.0, kRate);

      graph.deck(core::DeckId::A).loadTrack(trackA);
      graph.deck(core::DeckId::B).loadTrack(trackB);
      graph.deck(core::DeckId::C).loadTrack(trackC);
      graph.deck(core::DeckId::D).loadTrack(trackD);

      // Start all 4 decks with varispeed. Keylock (on by default) would put a time-stretcher on every deck; this soak
      // measures the varispeed path, the stretching decks have their own CPU test in test_dj_sound.cpp.
      for (std::size_t deckIndex = 0; deckIndex < core::kDeckCount; ++deckIndex) {
        graph.deck(deckIndex).setKeylock(false);
      }
      graph.deck(core::DeckId::A).play();
      graph.deck(core::DeckId::A).setPlaybackSpeed(1.04);
      graph.deck(core::DeckId::B).play();
      graph.deck(core::DeckId::B).setPlaybackSpeed(0.97);
      graph.deck(core::DeckId::C).play();
      graph.deck(core::DeckId::C).setPlaybackSpeed(1.02);
      graph.deck(core::DeckId::D).play();
      graph.deck(core::DeckId::D).setPlaybackSpeed(0.95);

      // EQs and filters active
      graph.channel(0).setEqDb(core::EqBand::Low, -4.0F);
      graph.channel(0).setFilter(0.3F);
      graph.channel(1).setEqDb(core::EqBand::High, 2.0F);
      graph.channel(1).setFilter(-0.2F);
      graph.channel(2).setEqDb(core::EqBand::Mid, -3.0F);
      graph.channel(3).setEqDb(core::EqBand::Low, 1.5F);

      // FX active in channel strips
      auto delay = std::make_unique<DelayEffect>();
      delay->prepare(kRate, blockSize);
      delay->setEnabled(true);
      graph.channel(0).fxSlot(0).setEffect(std::move(delay));

      auto reverb = std::make_unique<ReverbEffect>();
      reverb->prepare(kRate, blockSize);
      reverb->setEnabled(true);
      graph.channel(1).fxSlot(0).setEffect(std::move(reverb));

      // Buffer time budget in microseconds
      const double budgetUs = (static_cast<double>(blockSize) / kRate) * 1'000'000.0;

      std::vector<float> multiOut[4];
      for (auto& ch : multiOut) {
        ch.resize(static_cast<std::size_t>(blockSize), 0.0F);
      }
      float* outPtrs[4] = {multiOut[0].data(), multiOut[1].data(), multiOut[2].data(), multiOut[3].data()};

      // Soak test: 200 blocks
      constexpr int kSoakBlocks = 200;
      double totalElapsedUs = 0.0;
      double maxElapsedUs = 0.0;

      {
        ScopedRealtimeGuard rtGuard;
        for (int b = 0; b < kSoakBlocks; ++b) {
          const auto start = std::chrono::high_resolution_clock::now();
          graph.render(outPtrs, 4, blockSize);
          const auto end = std::chrono::high_resolution_clock::now();

          const double elapsedUs = std::chrono::duration<double, std::micro>(end - start).count();
          totalElapsedUs += elapsedUs;
          maxElapsedUs = std::max(maxElapsedUs, elapsedUs);
        }
      }

      const double avgElapsedUs = totalElapsedUs / static_cast<double>(kSoakBlocks);
      const double avgCpuPercent = (avgElapsedUs / budgetUs) * 100.0;

      // Zero dropouts/xruns: worst-case block finishes well within deadline
      CHECK(maxElapsedUs < budgetUs);
      CHECK(avgCpuPercent < 50.0);

      // Verify clean output
      for (int i = 0; i < blockSize; ++i) {
        CHECK(std::isfinite(multiOut[0][static_cast<std::size_t>(i)]));
        CHECK(std::isfinite(multiOut[1][static_cast<std::size_t>(i)]));
      }
    }
  }
}
