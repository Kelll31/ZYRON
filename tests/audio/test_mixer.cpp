// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Audio/DSP/MasterLimiter.hpp"
#include "Audio/DSP/Mixer.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron::audio;
using namespace zyron::test;
using Catch::Matchers::WithinAbs;

TEST_CASE("MasterLimiter is strictly realtime safe", "[audio][mixer][limiter][rt]") {
  MasterLimiter limiter;
  limiter.prepare(48000.0);

  std::vector<float> left(512, 0.5F);
  std::vector<float> right(512, -0.5F);

  {
    ScopedRealtimeGuard rtGuard;
    limiter.process(left.data(), right.data(), static_cast<int>(left.size()));
  }

  CHECK(limiter.currentGainReduction() <= 1.0F);
}

TEST_CASE("MasterLimiter enforces brickwall ceiling and prevents clipping", "[audio][mixer][limiter]") {
  constexpr double kRate = 48000.0;
  MasterLimiter limiter;
  limiter.prepare(kRate);
  limiter.setCeilingDb(-0.3F);  // ~0.966 linear

  const float ceilingLin = std::pow(10.0F, -0.3F * 0.05F);

  // Generate loud sine with amplitude 2.5 (+8 dBFS)
  auto loudSineL = generateSine(kRate, 1000.0, 0.05, 2.5F);
  auto loudSineR = generateSine(kRate, 1000.0, 0.05, 2.5F);
  const int numSamples = static_cast<int>(loudSineL.size());

  limiter.process(loudSineL.data(), loudSineR.data(), numSamples);

  // Check that no sample exceeds the ceiling
  float maxPeakL = 0.0F;
  float maxPeakR = 0.0F;
  for (int i = 0; i < numSamples; ++i) {
    maxPeakL = std::max(maxPeakL, std::abs(loudSineL[static_cast<std::size_t>(i)]));
    maxPeakR = std::max(maxPeakR, std::abs(loudSineR[static_cast<std::size_t>(i)]));
    CHECK(std::isfinite(loudSineL[static_cast<std::size_t>(i)]));
    CHECK(std::isfinite(loudSineR[static_cast<std::size_t>(i)]));
  }

  CHECK(maxPeakL <= ceilingLin + 1e-5F);
  CHECK(maxPeakR <= ceilingLin + 1e-5F);
  CHECK(limiter.currentGainReduction() < 0.5F);  // significant gain reduction applied
}

TEST_CASE("MasterLimiter lookahead delay matches configuration", "[audio][mixer][limiter]") {
  constexpr double kRate = 48000.0;
  MasterLimiter limiter;
  limiter.prepare(kRate);
  limiter.setLookaheadMs(1.0F);  // 48 samples @ 48 kHz

  std::vector<float> left(128, 0.0F);
  std::vector<float> right(128, 0.0F);
  left[0] = 0.5F;
  right[0] = 0.5F;

  limiter.process(left.data(), right.data(), static_cast<int>(left.size()));

  // Input impulse at 0 should appear at sample 48
  CHECK_THAT(left[0], WithinAbs(0.0F, 1e-4F));
  CHECK_THAT(left[48], WithinAbs(0.5F, 1e-3F));
}

TEST_CASE("Mixer is strictly realtime safe", "[audio][mixer][rt]") {
  Mixer mixer;
  mixer.prepare(48000.0);

  std::vector<float> leftA(256, 0.4F);
  std::vector<float> rightA(256, 0.4F);
  std::vector<float> leftB(256, 0.3F);
  std::vector<float> rightB(256, 0.3F);
  std::vector<float> outL(256, 0.0F);
  std::vector<float> outR(256, 0.0F);

  const float* inputsL[2] = {leftA.data(), leftB.data()};
  const float* inputsR[2] = {rightA.data(), rightB.data()};

  {
    ScopedRealtimeGuard rtGuard;
    mixer.process(leftA.data(), rightA.data(), leftB.data(), rightB.data(), outL.data(), outR.data(), 256);
    mixer.processCue(inputsL, inputsR, 2, outL.data(), outR.data(), 256);
  }

  CHECK(mixer.masterPeakLeft() > 0.0F);
  CHECK(mixer.masterPeakRight() > 0.0F);
}

TEST_CASE("Mixer crossfader curves", "[audio][mixer][crossfader]") {
  constexpr double kRate = 48000.0;
  Mixer mixer;
  mixer.prepare(kRate);
  mixer.masterLimiter().setLookaheadMs(0.0F);  // Instantaneous for direct sample checks
  mixer.glueCompressor().setEnabled(false);  // these checks need exact levels; the glue has its own tests
  mixer.masterLimiter().setCeilingDb(0.0F);    // 0 dBFS ceiling so unity 1.0 is not limited

  // Input DC signals: Deck A = 1.0, Deck B = 1.0
  std::vector<float> inA(512, 1.0F);
  std::vector<float> inB(512, 1.0F);
  std::vector<float> outL(512, 0.0F);
  std::vector<float> outR(512, 0.0F);

  SECTION("Linear curve") {
    mixer.setCrossfaderCurve(CrossfaderCurve::Linear);

    // Full Left (-1.0): Deck A = 1.0, Deck B = 0.0
    mixer.setCrossfader(-1.0F);
    mixer.reset();
    mixer.process(inA.data(), inA.data(), inB.data(), inB.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(1.0F, 0.01F));

    // Center (0.0): Deck A = 0.5, Deck B = 0.5 -> Sum = 1.0
    mixer.setCrossfader(0.0F);
    mixer.reset();
    mixer.process(inA.data(), inA.data(), inB.data(), inB.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(1.0F, 0.01F));

    // Full Right (+1.0): Deck A = 0.0, Deck B = 1.0
    mixer.setCrossfader(1.0F);
    mixer.reset();
    mixer.process(inA.data(), inA.data(), inB.data(), inB.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(1.0F, 0.01F));
  }

  SECTION("ConstantPower curve") {
    mixer.setCrossfaderCurve(CrossfaderCurve::ConstantPower);

    // Full Left (-1.0): Deck A = 1.0, Deck B = 0.0
    mixer.setCrossfader(-1.0F);
    mixer.reset();
    mixer.process(inA.data(), inA.data(), inB.data(), inB.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(1.0F, 0.01F));

    // Center (0.0): Deck A = 0.7071, Deck B = 0.7071 -> Sum = ~1.4142
    mixer.setCrossfader(0.0F);
    mixer.reset();
    // Test with only Deck A active to verify individual channel level
    std::vector<float> zeros(512, 0.0F);
    mixer.process(inA.data(), inA.data(), zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(0.7071F, 0.02F));

    // Full Right (+1.0)
    mixer.setCrossfader(1.0F);
    mixer.reset();
    mixer.process(zeros.data(), zeros.data(), inB.data(), inB.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(1.0F, 0.01F));
  }

  SECTION("Cut / Scratch curve") {
    mixer.setCrossfaderCurve(CrossfaderCurve::Cut);

    std::vector<float> zeros(512, 0.0F);

    // Full Left (-1.0): Deck A = 1.0, Deck B = 0.0
    mixer.setCrossfader(-1.0F);
    mixer.reset();
    mixer.process(zeros.data(), zeros.data(), inB.data(), inB.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(0.0F, 0.01F));

    // Slightly off Left (-0.8): Both decks should already be at full volume (1.0)
    mixer.setCrossfader(-0.8F);
    mixer.reset();
    mixer.process(zeros.data(), zeros.data(), inB.data(), inB.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(1.0F, 0.02F));

    // Center (0.0): Deck B still at 1.0
    mixer.setCrossfader(0.0F);
    mixer.reset();
    mixer.process(zeros.data(), zeros.data(), inB.data(), inB.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(1.0F, 0.02F));

    // Full Right (+1.0): Deck A cuts to 0.0
    mixer.setCrossfader(1.0F);
    mixer.reset();
    mixer.process(inA.data(), inA.data(), zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(0.0F, 0.01F));
  }
}

TEST_CASE("Mixer channel assign and Thru bypass", "[audio][mixer][routing]") {
  Mixer mixer;
  mixer.prepare(48000.0);
  mixer.masterLimiter().setLookaheadMs(0.0F);
  mixer.glueCompressor().setEnabled(false);  // these checks need exact levels; the glue has its own tests
  mixer.masterLimiter().setCeilingDb(0.0F);

  std::vector<float> inA(256, 0.6F);
  std::vector<float> inB(256, 0.4F);
  std::vector<float> outL(256, 0.0F);
  std::vector<float> outR(256, 0.0F);

  // Set crossfader to hard right (+1.0)
  mixer.setCrossfader(1.0F);

  // Deck A assigned to Thru -> should not be cut by crossfader
  mixer.setChannelAssign(0, CrossfaderAssign::Thru);
  mixer.setChannelAssign(1, CrossfaderAssign::Right);
  mixer.reset();

  mixer.process(inA.data(), inA.data(), inB.data(), inB.data(), outL.data(), outR.data(), 256);

  // Both Deck A (0.6) and Deck B (0.4) reach the master output
  CHECK_THAT(outL[255], WithinAbs(1.0F, 0.02F));
}

TEST_CASE("Mixer master gain and full mute kill floor", "[audio][mixer][gain]") {
  Mixer mixer;
  mixer.prepare(48000.0);
  mixer.masterLimiter().setLookaheadMs(0.0F);
  mixer.glueCompressor().setEnabled(false);  // these checks need exact levels; the glue has its own tests

  std::vector<float> in(512, 0.8F);
  std::vector<float> zeros(512, 0.0F);
  std::vector<float> outL(512, 0.0F);
  std::vector<float> outR(512, 0.0F);

  mixer.setCrossfader(-1.0F);  // Hard Left

  SECTION("Unity gain (0 dB)") {
    mixer.setMasterGainDb(0.0F);
    mixer.reset();
    mixer.process(in.data(), in.data(), zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(0.8F, 0.01F));
  }

  SECTION("-6 dB gain") {
    mixer.setMasterGainDb(-6.02F);
    mixer.reset();
    mixer.process(in.data(), in.data(), zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(0.4F, 0.02F));
  }

  SECTION("Kill floor (-60 dB)") {
    mixer.setMasterGainDb(-60.0F);
    mixer.reset();
    mixer.process(in.data(), in.data(), zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
    CHECK_THAT(outL[511], WithinAbs(0.0F, 1e-5F));
  }
}

TEST_CASE("Mixer cue bus routing", "[audio][mixer][cue]") {
  Mixer mixer;
  mixer.prepare(48000.0);

  std::vector<float> inA(128, 0.5F);
  std::vector<float> inB(128, 0.3F);
  std::vector<float> cueL(128, 0.0F);
  std::vector<float> cueR(128, 0.0F);

  const float* inputsL[2] = {inA.data(), inB.data()};
  const float* inputsR[2] = {inA.data(), inB.data()};

  // Only Deck A is cued
  mixer.setCue(0, true);
  mixer.setCue(1, false);
  mixer.processCue(inputsL, inputsR, 2, cueL.data(), cueR.data(), 128);

  CHECK_THAT(cueL[127], WithinAbs(0.5F, 1e-4F));

  // Both Deck A and B cued -> sum to 0.8
  mixer.setCue(1, true);
  mixer.processCue(inputsL, inputsR, 2, cueL.data(), cueR.data(), 128);

  CHECK_THAT(cueL[127], WithinAbs(0.8F, 1e-4F));
}

TEST_CASE("Mixer block size independence", "[audio][mixer][block_size]") {
  constexpr double kRate = 48000.0;
  constexpr int kTotalSamples = 2048;

  auto sigA = generateSine(kRate, 440.0, 2048.0 / kRate, 0.4F);
  auto sigB = generateSine(kRate, 880.0, 2048.0 / kRate, 0.4F);

  // Render reference in single 2048 block
  Mixer mixerRef;
  mixerRef.prepare(kRate);
  mixerRef.masterLimiter().setLookaheadMs(0.0F);
  mixerRef.glueCompressor().setEnabled(false);  // these checks need exact levels; the glue has its own tests
  mixerRef.setCrossfader(0.0F);
  mixerRef.reset();

  std::vector<float> refL(kTotalSamples);
  std::vector<float> refR(kTotalSamples);
  mixerRef.process(sigA.data(), sigA.data(), sigB.data(), sigB.data(), refL.data(), refR.data(), kTotalSamples);

  // Render in blocks of 64
  Mixer mixerBlock;
  mixerBlock.prepare(kRate);
  mixerBlock.masterLimiter().setLookaheadMs(0.0F);
  mixerBlock.glueCompressor().setEnabled(false);  // these checks need exact levels; the glue has its own tests
  mixerBlock.setCrossfader(0.0F);
  mixerBlock.reset();

  std::vector<float> blkL(kTotalSamples);
  std::vector<float> blkR(kTotalSamples);

  constexpr int kBlockSize = 64;
  for (int offset = 0; offset < kTotalSamples; offset += kBlockSize) {
    mixerBlock.process(sigA.data() + offset, sigA.data() + offset, sigB.data() + offset, sigB.data() + offset,
                       blkL.data() + offset, blkR.data() + offset, kBlockSize);
  }

  const float diff = nullTestMaxDiff(refL, blkL);
  CHECK(diff < 1e-4F);
}
