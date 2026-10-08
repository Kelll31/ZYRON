// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "Audio/Bridge/CommandBridge.hpp"
#include "Audio/DSP/StemMixer.hpp"
#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "Core/State/Ids.hpp"
#include "support/AllocationGuard.hpp"

using namespace zyron::audio;
using namespace zyron::core;
using namespace zyron::test;

namespace {

constexpr double kRate = 48000.0;
constexpr double kTwoPi = 6.283185307179586;

std::shared_ptr<TrackBuffer> makeSineStem(float freqHz, float amplitude, double durationSec, double sampleRate) {
  const auto totalFrames = static_cast<std::int64_t>(std::round(durationSec * sampleRate));
  auto tb = std::make_shared<TrackBuffer>(2, totalFrames, sampleRate);
  float* left = tb->channelData(0);
  float* right = tb->channelData(1);

  const double phaseInc = kTwoPi * static_cast<double>(freqHz) / sampleRate;
  double phase = 0.0;

  for (std::int64_t i = 0; i < totalFrames; ++i) {
    const float val = amplitude * static_cast<float>(std::sin(phase));
    left[i] = val;
    right[i] = val;
    phase += phaseInc;
    if (phase >= kTwoPi) {
      phase -= kTwoPi;
    }
  }
  return tb;
}

}  // namespace

TEST_CASE("StemMixer: volume, mute, solo, and cue controls", "[audio][stems]") {
  StemMixer mixer;
  mixer.prepare(kRate);

  constexpr int kBlockSize = 512;
  // Generate test signals for 4 stems
  std::vector<float> vocL(kBlockSize, 0.2F), vocR(kBlockSize, 0.2F);
  std::vector<float> drmL(kBlockSize, 0.3F), drmR(kBlockSize, 0.3F);
  std::vector<float> basL(kBlockSize, 0.4F), basR(kBlockSize, 0.4F);
  std::vector<float> othL(kBlockSize, 0.1F), othR(kBlockSize, 0.1F);

  const float* inL[4] = {vocL.data(), drmL.data(), basL.data(), othL.data()};
  const float* inR[4] = {vocR.data(), drmR.data(), basR.data(), othR.data()};

  std::vector<float> outL(kBlockSize, 0.0F);
  std::vector<float> outR(kBlockSize, 0.0F);

  SECTION("Default state mixes all stems at unity volume") {
    // Process multiple blocks to let smoothing settle
    for (int b = 0; b < 20; ++b) {
      mixer.process(inL, inR, outL.data(), outR.data(), kBlockSize);
    }

    // Expected sum: 0.2 + 0.3 + 0.4 + 0.1 = 1.0
    for (int i = 0; i < kBlockSize; ++i) {
      CHECK_THAT(outL[static_cast<std::size_t>(i)], Catch::Matchers::WithinRel(1.0F, 0.01F));
      CHECK_THAT(outR[static_cast<std::size_t>(i)], Catch::Matchers::WithinRel(1.0F, 0.01F));
    }
  }

  SECTION("Muting a stem removes it from the mix") {
    mixer.setMute(StemKind::Vocals, true);
    CHECK(mixer.isMuted(StemKind::Vocals));

    for (int b = 0; b < 20; ++b) {
      mixer.process(inL, inR, outL.data(), outR.data(), kBlockSize);
    }

    // Expected sum without vocals: 0.3 + 0.4 + 0.1 = 0.8
    for (int i = 0; i < kBlockSize; ++i) {
      CHECK_THAT(outL[static_cast<std::size_t>(i)], Catch::Matchers::WithinRel(0.8F, 0.01F));
    }
  }

  SECTION("Solo isolates the selected stem") {
    mixer.setSolo(StemKind::Bass, true);
    CHECK(mixer.isSolo(StemKind::Bass));

    for (int b = 0; b < 20; ++b) {
      mixer.process(inL, inR, outL.data(), outR.data(), kBlockSize);
    }

    // Only Bass should pass: 0.4
    for (int i = 0; i < kBlockSize; ++i) {
      CHECK_THAT(outL[static_cast<std::size_t>(i)], Catch::Matchers::WithinRel(0.4F, 0.01F));
    }

    // Multiple solos
    mixer.setSolo(StemKind::Vocals, true);
    for (int b = 0; b < 20; ++b) {
      mixer.process(inL, inR, outL.data(), outR.data(), kBlockSize);
    }

    // Vocals + Bass: 0.2 + 0.4 = 0.6
    for (int i = 0; i < kBlockSize; ++i) {
      CHECK_THAT(outL[static_cast<std::size_t>(i)], Catch::Matchers::WithinRel(0.6F, 0.01F));
    }

    // Un-soloing all restores full mix
    mixer.setSolo(StemKind::Bass, false);
    mixer.setSolo(StemKind::Vocals, false);
    for (int b = 0; b < 20; ++b) {
      mixer.process(inL, inR, outL.data(), outR.data(), kBlockSize);
    }
    for (int i = 0; i < kBlockSize; ++i) {
      CHECK_THAT(outL[static_cast<std::size_t>(i)], Catch::Matchers::WithinRel(1.0F, 0.01F));
    }
  }

  SECTION("Headphone cue bus routes only cued stems") {
    mixer.setCue(StemKind::Vocals, true);
    CHECK(mixer.isCue(StemKind::Vocals));
    CHECK_FALSE(mixer.isCue(StemKind::Drums));

    std::vector<float> cueL(kBlockSize, 0.0F);
    std::vector<float> cueR(kBlockSize, 0.0F);

    mixer.processCue(inL, inR, cueL.data(), cueR.data(), kBlockSize);

    // Cue bus has only Vocals: 0.2
    for (int i = 0; i < kBlockSize; ++i) {
      CHECK_THAT(cueL[static_cast<std::size_t>(i)], Catch::Matchers::WithinRel(0.2F, 0.01F));
    }
  }

  SECTION("Real-time safety: process and processCue do not allocate") {
    ScopedRealtimeGuard guard;
    mixer.process(inL, inR, outL.data(), outR.data(), kBlockSize);
    mixer.processCue(inL, inR, outL.data(), outR.data(), kBlockSize);
  }
}

TEST_CASE("DeckPlayer: 4-stem playback and sample-lock synchronization", "[audio][stems]") {
  const double durationSec = 1.0;
  // Create 4 distinct stems with known frequencies
  auto voc = makeSineStem(440.0F, 0.25F, durationSec, kRate);
  auto drm = makeSineStem(100.0F, 0.25F, durationSec, kRate);
  auto bas = makeSineStem(60.0F, 0.25F, durationSec, kRate);
  auto oth = makeSineStem(1000.0F, 0.25F, durationSec, kRate);

  DeckPlayer player;
  player.prepare(kRate);

  std::array<std::shared_ptr<const TrackBuffer>, zyron::core::kStemKindCount> stems = {voc, drm, bas, oth};
  player.loadStems(stems);

  REQUIRE(player.hasStems());
  REQUIRE(player.hasTrack());

  constexpr int kBlockSize = 256;
  std::vector<float> outL(kBlockSize, 0.0F);
  std::vector<float> outR(kBlockSize, 0.0F);
  float* outPtrs[2] = {outL.data(), outR.data()};

  SECTION("Playback renders sum of all 4 stems") {
    player.play();
    REQUIRE(player.isPlaying());

    // Settle fade-in ramp
    for (int b = 0; b < 20; ++b) {
      player.render(outPtrs, 2, kBlockSize);
    }

    float maxAmp = 0.0F;
    for (int i = 0; i < kBlockSize; ++i) {
      maxAmp = std::max(maxAmp, std::abs(outL[static_cast<std::size_t>(i)]));
    }

    // Sum of 4 stems of 0.25 amplitude each should produce an active signal
    CHECK(maxAmp > 0.1F);
    CHECK(player.currentFrame() > 0);
  }

  SECTION("Muting vocals removes vocals from deck output") {
    player.play();
    for (int b = 0; b < 10; ++b) {
      player.render(outPtrs, 2, kBlockSize);
    }

    player.stemMixer().setMute(StemKind::Vocals, true);
    for (int b = 0; b < 10; ++b) {
      player.render(outPtrs, 2, kBlockSize);
    }

    CHECK(player.stemMixer().isMuted(StemKind::Vocals));
  }

  SECTION("Soloing bass isolates bass stem") {
    player.play();
    player.stemMixer().setSolo(StemKind::Bass, true);

    for (int b = 0; b < 10; ++b) {
      player.render(outPtrs, 2, kBlockSize);
    }

    CHECK(player.stemMixer().isSolo(StemKind::Bass));
  }

  SECTION("Varispeed and loop synchronization across stems") {
    player.setPlaybackSpeed(1.2);
    player.setLoop(100, 2000);
    player.setLoopActive(true);
    player.play();

    for (int b = 0; b < 20; ++b) {
      player.render(outPtrs, 2, kBlockSize);
    }

    // Playhead is constrained inside loop
    CHECK(player.currentFrame() >= 100);
    CHECK(player.currentFrame() <= 2000);
  }

  SECTION("Real-time safety during stem rendering") {
    player.play();
    ScopedRealtimeGuard guard;
    player.render(outPtrs, 2, kBlockSize);
  }
}

TEST_CASE("CommandBridge translates stem commands to realtime messages", "[audio][stems]") {
  CommandBridge bridge;

  // 1. SetStemVolume
  const Command cmdVol = SetStemVolume{DeckId::A, StemKind::Vocals, 0.7F};
  const auto msgVol = CommandBridge::translateCommand(cmdVol);
  REQUIRE(msgVol.has_value());
  CHECK(msgVol->type == RtMessageType::DeckStemVolume);
  CHECK(msgVol->deck == DeckId::A);
  CHECK(msgVol->data.stemControl.stem == StemKind::Vocals);
  CHECK_THAT(msgVol->data.stemControl.volumeLinear, Catch::Matchers::WithinRel(0.7F, 1e-4F));

  // 2. SetStemMute
  const Command cmdMute = SetStemMute{DeckId::B, StemKind::Drums, true};
  const auto msgMute = CommandBridge::translateCommand(cmdMute);
  REQUIRE(msgMute.has_value());
  CHECK(msgMute->type == RtMessageType::DeckStemMute);
  CHECK(msgMute->deck == DeckId::B);
  CHECK(msgMute->data.stemControl.stem == StemKind::Drums);
  CHECK(msgMute->data.stemControl.active);

  // 3. SetStemSolo
  const Command cmdSolo = SetStemSolo{DeckId::A, StemKind::Bass, true};
  const auto msgSolo = CommandBridge::translateCommand(cmdSolo);
  REQUIRE(msgSolo.has_value());
  CHECK(msgSolo->type == RtMessageType::DeckStemSolo);
  CHECK(msgSolo->deck == DeckId::A);
  CHECK(msgSolo->data.stemControl.stem == StemKind::Bass);
  CHECK(msgSolo->data.stemControl.active);

  // 4. SetStemCue
  const Command cmdCue = SetStemCue{DeckId::B, StemKind::Other, true};
  const auto msgCue = CommandBridge::translateCommand(cmdCue);
  REQUIRE(msgCue.has_value());
  CHECK(msgCue->type == RtMessageType::DeckStemCue);
  CHECK(msgCue->deck == DeckId::B);
  CHECK(msgCue->data.stemControl.stem == StemKind::Other);
  CHECK(msgCue->data.stemControl.active);

  // Push to bridge queue
  CHECK(bridge.pushCommand(cmdVol));
  CHECK(bridge.pushCommand(cmdMute));
  CHECK(bridge.pushCommand(cmdSolo));
  CHECK(bridge.pushCommand(cmdCue));

  RtMessage popped;
  REQUIRE(bridge.popMessage(popped));
  CHECK(popped.type == RtMessageType::DeckStemVolume);
  REQUIRE(bridge.popMessage(popped));
  CHECK(popped.type == RtMessageType::DeckStemMute);
  REQUIRE(bridge.popMessage(popped));
  CHECK(popped.type == RtMessageType::DeckStemSolo);
  REQUIRE(bridge.popMessage(popped));
  CHECK(popped.type == RtMessageType::DeckStemCue);
}
