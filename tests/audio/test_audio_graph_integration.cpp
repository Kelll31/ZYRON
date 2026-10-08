// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <numeric>
#include <vector>

#include "Audio/Bridge/CommandBridge.hpp"
#include "Audio/DSP/ChannelStrip.hpp"
#include "Audio/DSP/Mixer.hpp"
#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Effects/DelayEffect.hpp"
#include "Audio/Effects/EffectRegistry.hpp"
#include "Audio/Routing/CueRouter.hpp"
#include "Core/Audio/AudioTap.hpp"
#include "Recording/MasterRecorder.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron::audio;
using namespace zyron::recording;
using namespace zyron::test;

namespace {

constexpr double kRate = 48000.0;

std::shared_ptr<TrackBuffer> makeSineTrack(float freqHz, double durationSec, double sampleRate) {
  const auto totalFrames = static_cast<std::int64_t>(std::round(durationSec * sampleRate));
  auto tb = std::make_shared<TrackBuffer>(2, totalFrames, sampleRate);
  float* left = tb->channelData(0);
  float* right = tb->channelData(1);

  constexpr double kTwoPi = 6.283185307179586;
  const double phaseInc = kTwoPi * static_cast<double>(freqHz) / sampleRate;
  double phase = 0.0;

  for (std::int64_t i = 0; i < totalFrames; ++i) {
    const float val = static_cast<float>(std::sin(phase));
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

TEST_CASE("Audio graph block-size independence (32..2048 samples)", "[audio][integration]") {
  const auto blockSizes = {32, 64, 128, 256, 512, 1024, 2048};
  constexpr int kTotalSamples = 8192;

  // Multi-frequency test signal
  std::vector<float> inputL(kTotalSamples);
  std::vector<float> inputR(kTotalSamples);
  for (int i = 0; i < kTotalSamples; ++i) {
    const double t = static_cast<double>(i) / kRate;
    const float val = static_cast<float>(0.3 * std::sin(2.0 * 3.14159265 * 440.0 * t) +
                                         0.2 * std::sin(2.0 * 3.14159265 * 1500.0 * t));
    inputL[static_cast<std::size_t>(i)] = val;
    inputR[static_cast<std::size_t>(i)] = val;
  }

  std::vector<float> referenceL;
  std::vector<float> referenceR;

  for (int blockSize : blockSizes) {
    DYNAMIC_SECTION("block size = " << blockSize) {
      ChannelStrip strip;
      strip.prepare(kRate);

      Mixer mixer;
      mixer.prepare(kRate);
      mixer.setCrossfader(0.0F);  // Left channel full
      mixer.setChannelAssign(0, CrossfaderAssign::Thru);

      std::vector<float> runL = inputL;
      std::vector<float> runR = inputR;
      std::vector<float> mixOutL(kTotalSamples, 0.0F);
      std::vector<float> mixOutR(kTotalSamples, 0.0F);

      for (int done = 0; done < kTotalSamples;) {
        const int n = std::min(blockSize, kTotalSamples - done);
        float* stripPtrs[2] = {runL.data() + done, runR.data() + done};
        strip.process(stripPtrs, 2, n);

        const float* inL[1] = {stripPtrs[0]};
        const float* inR[1] = {stripPtrs[1]};

        mixer.process(inL, inR, 1, mixOutL.data() + done, mixOutR.data() + done, n);
        done += n;
      }

      if (referenceL.empty()) {
        referenceL = mixOutL;
        referenceR = mixOutR;
      } else {
        // Assert varying block size produces identical output within parameter-smoothing settling
        for (int i = 300; i < kTotalSamples; ++i) {
          const auto idx = static_cast<std::size_t>(i);
          CHECK_THAT(mixOutL[idx], Catch::Matchers::WithinAbs(referenceL[idx], 0.005F));
          CHECK_THAT(mixOutR[idx], Catch::Matchers::WithinAbs(referenceR[idx], 0.005F));
        }
      }
    }
  }
}

TEST_CASE("Audio graph null test (phase cancellation)", "[audio][integration]") {
  Mixer mixer;
  mixer.prepare(kRate);
  mixer.setCrossfaderCurve(CrossfaderCurve::Linear);
  mixer.setCrossfader(0.0F);  // Center
  mixer.setChannelAssign(0, CrossfaderAssign::Thru);
  mixer.setChannelAssign(1, CrossfaderAssign::Thru);

  ChannelStrip strip0;
  strip0.prepare(kRate);

  ChannelStrip strip1;
  strip1.prepare(kRate);

  constexpr int kNumSamples = 1024;
  std::vector<float> sig0L(kNumSamples, 0.4F);
  std::vector<float> sig0R(kNumSamples, 0.4F);
  std::vector<float> sig1L(kNumSamples, -0.4F);  // Inverted phase (-1.0)
  std::vector<float> sig1R(kNumSamples, -0.4F);

  // Warm up smoothing
  std::vector<float> warm0L(512, 0.0F);
  std::vector<float> warm0R(512, 0.0F);
  std::vector<float> warm1L(512, 0.0F);
  std::vector<float> warm1R(512, 0.0F);
  float* w0[2] = {warm0L.data(), warm0R.data()};
  float* w1[2] = {warm1L.data(), warm1R.data()};
  strip0.process(w0, 2, 512);
  strip1.process(w1, 2, 512);

  float* s0[2] = {sig0L.data(), sig0R.data()};
  float* s1[2] = {sig1L.data(), sig1R.data()};
  strip0.process(s0, 2, kNumSamples);
  strip1.process(s1, 2, kNumSamples);

  std::vector<float> outL(kNumSamples, 0.0F);
  std::vector<float> outR(kNumSamples, 0.0F);
  const float* inL[2] = {sig0L.data(), sig1L.data()};
  const float* inR[2] = {sig0R.data(), sig1R.data()};

  mixer.process(inL, inR, 2, outL.data(), outR.data(), kNumSamples);

  // Assert near complete cancellation
  for (int i = 200; i < kNumSamples; ++i) {
    const auto idx = static_cast<std::size_t>(i);
    CHECK_THAT(outL[idx], Catch::Matchers::WithinAbs(0.0F, 0.001F));
    CHECK_THAT(outR[idx], Catch::Matchers::WithinAbs(0.0F, 0.001F));
  }
}

TEST_CASE("Audio graph no clipping / NaN under extreme abuse", "[audio][integration]") {
  ChannelStrip strip;
  strip.prepare(kRate);

  // Extreme EQ boost (+12 dB), maximum filter resonance
  strip.setLowDb(12.0F);
  strip.setMidDb(12.0F);
  strip.setHighDb(12.0F);
  strip.setGainDb(12.0F);
  strip.setFilter(0.8F);
  strip.setFilterResonance(4.0F);

  // Delay with high feedback
  auto delay = std::make_unique<DelayEffect>();
  delay->setFeedback(0.95F);
  delay->setDelayTimeMs(20.0F);
  delay->setDryWet(0.7F);
  strip.fxSlot(0).setEffect(std::move(delay));

  Mixer mixer;
  mixer.prepare(kRate);
  mixer.setMasterGainDb(12.0F);
  mixer.masterLimiter().setCeilingDb(-0.3F);

  // Input spikes +/- 100.0F
  constexpr int kSamples = 2048;
  std::vector<float> badL(kSamples);
  std::vector<float> badR(kSamples);
  for (int i = 0; i < kSamples; ++i) {
    badL[static_cast<std::size_t>(i)] = (i % 2 == 0) ? 50.0F : -50.0F;
    badR[static_cast<std::size_t>(i)] = (i % 3 == 0) ? 100.0F : -25.0F;
  }

  std::vector<float> outL(kSamples, 0.0F);
  std::vector<float> outR(kSamples, 0.0F);

  float* sPtrs[2] = {badL.data(), badR.data()};
  strip.process(sPtrs, 2, kSamples);

  const float* inL[1] = {badL.data()};
  const float* inR[1] = {badR.data()};

  mixer.process(inL, inR, 1, outL.data(), outR.data(), kSamples);

  // All output samples strictly finite and clamped below limiter ceiling
  const float maxAllowedCeiling = std::pow(10.0F, -0.3F * 0.05F) + 0.05F;  // ~1.01
  for (int i = 0; i < kSamples; ++i) {
    const auto idx = static_cast<std::size_t>(i);
    REQUIRE(std::isfinite(outL[idx]));
    REQUIRE(std::isfinite(outR[idx]));
    CHECK(std::abs(outL[idx]) <= maxAllowedCeiling);
    CHECK(std::abs(outR[idx]) <= maxAllowedCeiling);
  }
}

TEST_CASE("End-to-end audio pipeline: Deck -> Strip -> Mixer -> Cue -> Recorder", "[audio][integration]") {
  const auto track = makeSineTrack(440.0F, 1.0, kRate);

  DeckPlayer deckA;
  deckA.prepare(kRate);
  deckA.loadTrack(track);
  deckA.play();

  DeckPlayer deckB;
  deckB.prepare(kRate);
  deckB.loadTrack(track);
  deckB.play();

  ChannelStrip stripA;
  stripA.prepare(kRate);

  ChannelStrip stripB;
  stripB.prepare(kRate);

  Mixer mixer;
  mixer.prepare(kRate);
  mixer.setCrossfader(-1.0F);  // 100% on Deck A (Deck B silenced on master)
  mixer.setCue(1, true);       // PFL headphone cue on Deck B

  CueRouter cueRouter;
  cueRouter.prepare(kRate);
  cueRouter.setMode(HeadphoneRoutingMode::MultiChannel);
  cueRouter.setHeadphoneMix(0.0F);  // 100% Cue signal on headphone channels (channels 2, 3)

  MasterRecorder recorder;
  const auto tempWav = std::filesystem::temp_directory_path() / "zyron_e2e_test.wav";
  REQUIRE(recorder.start(tempWav, kRate, 2));

  constexpr int kBlockSize = 256;
  constexpr int kTotalBlocks = 16;

  std::vector<float> deckBufAL(kBlockSize);
  std::vector<float> deckBufAR(kBlockSize);
  std::vector<float> deckBufBL(kBlockSize);
  std::vector<float> deckBufBR(kBlockSize);

  std::vector<float> masterOutL(kBlockSize);
  std::vector<float> masterOutR(kBlockSize);
  std::vector<float> cueBusL(kBlockSize);
  std::vector<float> cueBusR(kBlockSize);

  std::vector<float> multiOut[4];
  for (auto& ch : multiOut) {
    ch.resize(kBlockSize, 0.0F);
  }

  // Strictly realtime safe loop
  {
    ScopedRealtimeGuard rtGuard;

    for (int b = 0; b < kTotalBlocks; ++b) {
      // 1. Decks render audio
      float* ptrA[2] = {deckBufAL.data(), deckBufAR.data()};
      deckA.render(ptrA, 2, kBlockSize);

      float* ptrB[2] = {deckBufBL.data(), deckBufBR.data()};
      deckB.render(ptrB, 2, kBlockSize);

      // 2. Channel strips
      stripA.process(ptrA, 2, kBlockSize);
      stripB.process(ptrB, 2, kBlockSize);

      // 3. Mixer
      const float* inL[2] = {deckBufAL.data(), deckBufBL.data()};
      const float* inR[2] = {deckBufAR.data(), deckBufBR.data()};

      mixer.process(inL, inR, 2, masterOutL.data(), masterOutR.data(), kBlockSize);
      mixer.processCue(inL, inR, 2, cueBusL.data(), cueBusR.data(), kBlockSize);

      // 4. Cue router (Master -> ch 0,1; Headphone Cue -> ch 2,3)
      float* outChs[4] = {multiOut[0].data(), multiOut[1].data(), multiOut[2].data(), multiOut[3].data()};
      cueRouter.route(masterOutL.data(), masterOutR.data(), cueBusL.data(), cueBusR.data(), outChs, 4, kBlockSize);

      // 5. Recorder master tap
      float* mOut[2] = {masterOutL.data(), masterOutR.data()};
      recorder.writeSamples(mOut, 2, kBlockSize);
    }
  }

  recorder.stop();
  std::filesystem::remove(tempWav);

  // Deck A is on Master (channels 0, 1), Deck B is on Cue (channels 2, 3)
  float maxMaster = 0.0F;
  float maxCue = 0.0F;
  for (int i = 0; i < kBlockSize; ++i) {
    maxMaster = std::max(maxMaster, std::abs(multiOut[0][static_cast<std::size_t>(i)]));
    maxCue = std::max(maxCue, std::abs(multiOut[2][static_cast<std::size_t>(i)]));
  }

  CHECK(maxMaster > 0.05F);
  CHECK(maxCue > 0.05F);
}
