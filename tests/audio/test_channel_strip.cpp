// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "Audio/DSP/ChannelStrip.hpp"
#include "Audio/DSP/DjFilter.hpp"
#include "Audio/DSP/StateVariableFilter.hpp"
#include "Audio/DSP/ThreeBandEq.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using zyron::audio::ChannelStrip;
using zyron::audio::DjFilter;
using zyron::audio::StateVariableFilter;
using zyron::audio::ThreeBandEq;
using namespace zyron::test;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> processThroughEq(ThreeBandEq& eq, const std::vector<float>& input, int blockSize = 256) {
  std::vector<float> out = input;
  const int total = static_cast<int>(input.size());
  for (int done = 0; done < total;) {
    const int n = std::min(blockSize, total - done);
    float* ptr[1] = {out.data() + done};
    eq.process(ptr, 1, n);
    done += n;
  }
  return out;
}

std::vector<float> processThroughFilter(DjFilter& filter, const std::vector<float>& input, int blockSize = 256) {
  std::vector<float> out = input;
  const int total = static_cast<int>(input.size());
  for (int done = 0; done < total;) {
    const int n = std::min(blockSize, total - done);
    float* ptr[1] = {out.data() + done};
    filter.process(ptr, 1, n);
    done += n;
  }
  return out;
}

}  // namespace

TEST_CASE("StateVariableFilter behaves correctly and remains stable") {
  StateVariableFilter svf;
  svf.prepare(kRate);

  SECTION("filter handles impulse response cleanly without NaN") {
    svf.setParameters(1000.0F, 0.7071F);
    const auto imp = generateImpulse(512, 0, 1.0F);
    std::vector<float> lp(512);

    for (std::size_t i = 0; i < 512; ++i) {
      const auto out = svf.process(imp[i]);
      lp[i] = out.lowpass;
    }

    CHECK(isFinite(lp));
    CHECK(isWithinClip(lp, 1.1F));
  }
}

TEST_CASE("ThreeBandEq frequency response and kill behavior") {
  ThreeBandEq eq;
  eq.prepare(kRate);

  SECTION("flat EQ (0 dB on all bands) preserves signal") {
    const auto sine = generateSine(kRate, 1000.0, 0.1, 0.8F);
    // Let smoothers settle
    std::vector<float> warmup(480, 0.0F);
    float* warmPtr[1] = {warmup.data()};
    eq.process(warmPtr, 1, 480);

    const auto out = processThroughEq(eq, sine);
    CHECK(isFinite(out));
    // Within negligible crossover error
    CHECK_THAT(measureMagnitudeAt(out, kRate, 1000.0), Catch::Matchers::WithinAbs(0.8F, 0.05F));
  }

  SECTION("killing Low band (-60 dB) eliminates 80 Hz bass") {
    eq.setLowDb(-60.0F);
    const auto bass = generateSine(kRate, 80.0, 0.2, 1.0F);

    // Warm up smoothers
    std::vector<float> warmup(4800, 0.0F);
    float* warmPtr[1] = {warmup.data()};
    eq.process(warmPtr, 1, 4800);

    const auto out = processThroughEq(eq, bass);
    const float remainingBass = measureMagnitudeAt(out, kRate, 80.0);
    // Over 30 dB attenuation on 80 Hz
    CHECK(remainingBass < 0.03F);
  }

  SECTION("killing High band (-60 dB) eliminates 10 kHz treble") {
    eq.setHighDb(-60.0F);
    const auto treble = generateSine(kRate, 10000.0, 0.1, 1.0F);

    std::vector<float> warmup(4800, 0.0F);
    float* warmPtr[1] = {warmup.data()};
    eq.process(warmPtr, 1, 4800);

    const auto out = processThroughEq(eq, treble);
    const float remainingTreble = measureMagnitudeAt(out, kRate, 10000.0);
    CHECK(remainingTreble < 0.03F);
  }

  SECTION("EQ process is realtime safe") {
    std::vector<float> buf(512, 0.5F);
    float* channels[1] = {buf.data()};

    ScopedRealtimeGuard guard;
    eq.process(channels, 1, 512);
    REQUIRE_FALSE(guard.hasViolations());
  }
}

TEST_CASE("DjFilter bipolar HPF/LPF behavior") {
  DjFilter filter;
  filter.prepare(kRate);

  SECTION("center knob (0.0) is neutral bypass") {
    filter.setFilter(0.0F);
    const auto sine = generateSine(kRate, 1000.0, 0.05, 0.7F);
    const auto out = processThroughFilter(filter, sine);
    CHECK(out == sine);
  }

  SECTION("LPF knob (-0.9) attenuates 8 kHz highs while passing 100 Hz bass") {
    filter.setFilter(-0.9F);
    std::vector<float> warmup(4800, 0.0F);
    float* warmPtr[1] = {warmup.data()};
    filter.process(warmPtr, 1, 4800);

    const auto treble = generateSine(kRate, 8000.0, 0.1, 1.0F);
    const auto out = processThroughFilter(filter, treble);
    CHECK(measureMagnitudeAt(out, kRate, 8000.0) < 0.05F);
  }

  SECTION("HPF knob (+0.9) attenuates 100 Hz bass while passing 8 kHz highs") {
    filter.setFilter(0.9F);
    std::vector<float> warmup(4800, 0.0F);
    float* warmPtr[1] = {warmup.data()};
    filter.process(warmPtr, 1, 4800);

    const auto bass = generateSine(kRate, 100.0, 0.1, 1.0F);
    const auto out = processThroughFilter(filter, bass);
    CHECK(measureMagnitudeAt(out, kRate, 100.0) < 0.05F);
  }

  SECTION("Filter process is realtime safe") {
    std::vector<float> buf(512, 0.5F);
    float* channels[1] = {buf.data()};

    ScopedRealtimeGuard guard;
    filter.process(channels, 1, 512);
    REQUIRE_FALSE(guard.hasViolations());
  }
}

TEST_CASE("ChannelStrip integrates trim, EQ, filter, volume, and meters") {
  ChannelStrip strip;
  strip.prepare(kRate);

  SECTION("volume fader at 0.0 produces silence") {
    strip.setVolume(0.0F);
    std::vector<float> warmup(4800, 0.0F);
    float* warmPtr[1] = {warmup.data()};
    strip.process(warmPtr, 1, 4800);

    std::vector<float> buf = generateSine(kRate, 1000.0, 0.05, 1.0F);
    float* channels[1] = {buf.data()};
    strip.process(channels, 1, static_cast<int>(buf.size()));
    CHECK(maxAbsolute(buf) < 1e-4F);
  }

  SECTION("mute produces silence") {
    strip.setMute(true);
    std::vector<float> warmup(4800, 0.0F);
    float* warmPtr[1] = {warmup.data()};
    strip.process(warmPtr, 1, 4800);

    std::vector<float> buf = generateSine(kRate, 1000.0, 0.05, 1.0F);
    float* channels[1] = {buf.data()};
    strip.process(channels, 1, static_cast<int>(buf.size()));
    CHECK(maxAbsolute(buf) < 1e-4F);
  }

  SECTION("peak meter tracks non-silent audio") {
    strip.setVolume(1.0F);
    strip.setMute(false);
    std::vector<float> buf = generateSine(kRate, 1000.0, 0.1, 0.8F);
    float* channels[1] = {buf.data()};
    strip.process(channels, 1, static_cast<int>(buf.size()));

    CHECK(strip.peakLeft() > 0.5F);
  }

  SECTION("ChannelStrip is realtime safe (zero allocations during process)") {
    std::vector<float> l(512, 0.5F);
    std::vector<float> r(512, 0.5F);
    float* channels[2] = {l.data(), r.data()};

    ScopedRealtimeGuard guard;
    strip.process(channels, 2, 512);
    REQUIRE_FALSE(guard.hasViolations());
    REQUIRE(guard.allocationCount() == 0);
    REQUIRE(guard.deallocationCount() == 0);
  }
}
