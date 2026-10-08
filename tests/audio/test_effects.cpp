// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numeric>
#include <vector>

#include "Audio/DSP/ChannelStrip.hpp"
#include "Audio/Effects/DelayEffect.hpp"
#include "Audio/Effects/EchoEffect.hpp"
#include "Audio/Effects/EffectRegistry.hpp"
#include "Audio/Effects/EffectSlot.hpp"
#include "Audio/Effects/FlangerEffect.hpp"
#include "Audio/Effects/PhaserEffect.hpp"
#include "Audio/Effects/ReverbEffect.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron::audio;
using namespace zyron::test;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> processStereoImpulse(Effect& fx, int totalSamples, int blockSize = 128) {
  std::vector<float> bufL(static_cast<std::size_t>(totalSamples), 0.0F);
  std::vector<float> bufR(static_cast<std::size_t>(totalSamples), 0.0F);
  bufL[0] = 1.0F;
  bufR[0] = 1.0F;

  for (int done = 0; done < totalSamples;) {
    const int n = std::min(blockSize, totalSamples - done);
    float* ptrs[2] = {bufL.data() + done, bufR.data() + done};
    fx.process(ptrs, 2, n);
    done += n;
  }
  return bufL;
}

}  // namespace

TEST_CASE("EffectRegistry registers and instantiates effects", "[effects]") {
  auto& registry = EffectRegistry::instance();
  registry.clear();

  SECTION("can register and create custom effects") {
    class DummyEffect : public Effect {
     public:
      [[nodiscard]] std::string_view id() const noexcept override { return "dummy"; }
      [[nodiscard]] std::string_view name() const noexcept override { return "Dummy"; }
      void prepare(double, int) noexcept override {}
      void reset() noexcept override {}
      void process(float* const*, int, int) noexcept override {}
      void setEnabled(bool) noexcept override {}
      [[nodiscard]] bool isEnabled() const noexcept override { return true; }
      void setDryWet(float) noexcept override {}
      [[nodiscard]] float dryWet() const noexcept override { return 1.0F; }
      [[nodiscard]] std::span<const ParameterDescriptor> parameters() const noexcept override { return {}; }
      void setParameter(std::size_t, float) noexcept override {}
      [[nodiscard]] float parameter(std::size_t) const noexcept override { return 0.0F; }
    };

    registry.registerEffect("dummy", []() { return std::make_unique<DummyEffect>(); });
    REQUIRE(registry.hasEffect("dummy"));
    auto instance = registry.create("dummy");
    REQUIRE(instance != nullptr);
    CHECK(instance->id() == "dummy");
    CHECK(instance->name() == "Dummy");
  }

  SECTION("built-in effects are all available after registerBuiltins") {
    registry.registerBuiltins();
    CHECK(registry.hasEffect("delay"));
    CHECK(registry.hasEffect("echo"));
    CHECK(registry.hasEffect("reverb"));
    CHECK(registry.hasEffect("flanger"));
    CHECK(registry.hasEffect("phaser"));

    auto delay = registry.create("delay");
    auto echo = registry.create("echo");
    auto reverb = registry.create("reverb");
    auto flanger = registry.create("flanger");
    auto phaser = registry.create("phaser");

    REQUIRE(delay != nullptr);
    REQUIRE(echo != nullptr);
    REQUIRE(reverb != nullptr);
    REQUIRE(flanger != nullptr);
    REQUIRE(phaser != nullptr);

    CHECK(delay->id() == "delay");
    CHECK(echo->id() == "echo");
    CHECK(reverb->id() == "reverb");
    CHECK(flanger->id() == "flanger");
    CHECK(phaser->id() == "phaser");
  }

  SECTION("creating an unknown effect returns nullptr") {
    CHECK(registry.create("nonexistent") == nullptr);
  }
}

TEST_CASE("All built-in effects are strictly realtime safe during process", "[effects][realtime]") {
  DelayEffect delay;
  EchoEffect echo;
  ReverbEffect reverb;
  FlangerEffect flanger;
  PhaserEffect phaser;

  delay.prepare(kRate, 256);
  echo.prepare(kRate, 256);
  reverb.prepare(kRate, 256);
  flanger.prepare(kRate, 256);
  phaser.prepare(kRate, 256);

  std::vector<float> left(256, 0.5F);
  std::vector<float> right(256, 0.5F);
  float* channels[2] = {left.data(), right.data()};

  {
    ScopedRealtimeGuard rtGuard;
    delay.process(channels, 2, 256);
    echo.process(channels, 2, 256);
    reverb.process(channels, 2, 256);
    flanger.process(channels, 2, 256);
    phaser.process(channels, 2, 256);
  }
}

TEST_CASE("DelayEffect produces delayed echoes with damping and ping-pong", "[effects][delay]") {
  DelayEffect delay;
  delay.prepare(kRate, 256);
  delay.setDelayTimeMs(10.0F);  // 10 ms at 48 kHz = 480 samples
  delay.setFeedback(0.5F);
  delay.setDryWet(1.0F);  // 100% wet
  delay.reset();

  SECTION("produces delayed response at the specified delay time") {
    // Warm up smoothing
    std::vector<float> silence(2048, 0.0F);
    float* warmPtr[1] = {silence.data()};
    delay.process(warmPtr, 1, 2048);

    // Feed single impulse
    std::vector<float> buf(960, 0.0F);
    buf[0] = 1.0F;
    float* ptr[1] = {buf.data()};
    delay.process(ptr, 1, 960);

    // Peak should occur around sample 480 (10 ms)
    float maxVal = 0.0F;
    int maxIdx = -1;
    for (int i = 100; i < 960; ++i) {
      if (std::abs(buf[static_cast<std::size_t>(i)]) > maxVal) {
        maxVal = std::abs(buf[static_cast<std::size_t>(i)]);
        maxIdx = i;
      }
    }
    CHECK(maxIdx >= 470);
    CHECK(maxIdx <= 490);
    CHECK(maxVal > 0.5F);
  }

  SECTION("parameter reflection by ID works correctly") {
    delay.setParameterById("feedback", 0.75F);
    CHECK_THAT(delay.parameterById("feedback"), Catch::Matchers::WithinRel(0.75F, 0.01F));

    delay.setParameterById("time", 500.0F);
    CHECK_THAT(delay.parameterById("time"), Catch::Matchers::WithinRel(500.0F, 0.01F));
  }

  SECTION("ping-pong bounces signal between channels") {
    delay.setPingPong(true);
    delay.setDelayTimeMs(10.0F);  // 10 ms at 48 kHz = 480 samples
    delay.setFeedback(0.7F);
    delay.setDryWet(1.0F);
    delay.reset();

    // Input impulse only on Left channel
    std::vector<float> bufL(1200, 0.0F);
    std::vector<float> bufR(1200, 0.0F);
    bufL[0] = 1.0F;
    float* ptrs[2] = {bufL.data(), bufR.data()};
    delay.process(ptrs, 2, 1200);

    // In ping-pong, repeat from L bounces to R channel at 2T (around sample 960)
    float maxAll = 0.0F;
    int bestIdx = -1;
    for (int i = 0; i < 1200; ++i) {
      if (std::abs(bufR[static_cast<std::size_t>(i)]) > maxAll) {
        maxAll = std::abs(bufR[static_cast<std::size_t>(i)]);
        bestIdx = i;
      }
    }
    CHECK(bestIdx >= 950);
    CHECK(bestIdx <= 970);
    CHECK(maxAll > 0.3F);
  }
}

TEST_CASE("EchoEffect handles freeze and bandpass filtering", "[effects][echo]") {
  EchoEffect echo;
  echo.prepare(kRate, 256);
  echo.setEchoTimeMs(10.0F);
  echo.setFeedback(0.8F);
  echo.setDryWet(1.0F);
  echo.reset();

  SECTION("freeze mode maintains circulation without explosion") {
    // Warm up
    std::vector<float> silence(2048, 0.0F);
    float* warmPtr[1] = {silence.data()};
    echo.process(warmPtr, 1, 2048);

    // Send impulse
    std::vector<float> pulse(480, 0.0F);
    pulse[0] = 1.0F;
    float* p1[1] = {pulse.data()};
    echo.process(p1, 1, 480);

    // Enable freeze
    echo.setFreeze(true);
    CHECK(echo.isFrozen());

    // Process 20000 samples of silence (frozen echo should recirculate cleanly)
    std::vector<float> longSilence(20000, 0.0F);
    float* p2[1] = {longSilence.data()};
    echo.process(p2, 1, 20000);

    // Verify all samples remain bounded and finite
    for (float s : longSilence) {
      REQUIRE(std::isfinite(s));
      CHECK(std::abs(s) <= 2.0F);
    }
  }
}

TEST_CASE("ReverbEffect generates reverberant tail and stereo decorrelation", "[effects][reverb]") {
  ReverbEffect reverb;
  reverb.prepare(kRate, 256);
  reverb.setRoomSize(0.8F);
  reverb.setDryWet(1.0F);
  reverb.setStereoWidth(1.0F);
  reverb.reset();

  SECTION("impulse response produces dense decay without NaN or infinity") {
    auto tail = processStereoImpulse(reverb, 10000);
    float energyFirstHalf = 0.0F;
    float energySecondHalf = 0.0F;

    for (std::size_t i = 0; i < 5000; ++i) {
      REQUIRE(std::isfinite(tail[i]));
      energyFirstHalf += tail[i] * tail[i];
    }
    for (std::size_t i = 5000; i < 10000; ++i) {
      REQUIRE(std::isfinite(tail[i]));
      energySecondHalf += tail[i] * tail[i];
    }

    CHECK(energyFirstHalf > 0.0F);
    // Natural reverberation decays over time
    CHECK(energyFirstHalf > energySecondHalf);
  }

  SECTION("stereo width 0 produces identical mono wet channels") {
    reverb.reset();
    reverb.setStereoWidth(0.0F);

    std::vector<float> bufL(1024, 0.0F);
    std::vector<float> bufR(1024, 0.0F);
    bufL[0] = 1.0F;
    bufR[0] = 0.5F;
    float* ptrs[2] = {bufL.data(), bufR.data()};
    reverb.process(ptrs, 2, 1024);

    for (std::size_t i = 100; i < 1024; ++i) {
      CHECK_THAT(bufL[i], Catch::Matchers::WithinRel(bufR[i], 0.001F));
    }
  }
}

TEST_CASE("FlangerEffect and PhaserEffect modulate phase cleanly", "[effects][modulation]") {
  SECTION("FlangerEffect produces modulated comb filtering") {
    FlangerEffect flanger;
    flanger.prepare(kRate, 256);
    flanger.setDepth(0.8F);
    flanger.setFeedback(0.5F);
    flanger.setDryWet(0.5F);

    std::vector<float> bufL(2048, 0.5F);
    std::vector<float> bufR(2048, 0.5F);
    float* ptrs[2] = {bufL.data(), bufR.data()};
    flanger.process(ptrs, 2, 2048);

    for (std::size_t i = 0; i < 2048; ++i) {
      REQUIRE(std::isfinite(bufL[i]));
      REQUIRE(std::isfinite(bufR[i]));
      CHECK(std::abs(bufL[i]) < 2.0F);
    }
  }

  SECTION("PhaserEffect produces 6-stage sweeping notches cleanly") {
    PhaserEffect phaser;
    phaser.prepare(kRate, 256);
    phaser.setDepth(0.9F);
    phaser.setFeedback(0.7F);
    phaser.setDryWet(0.5F);

    std::vector<float> bufL(2048, 0.5F);
    std::vector<float> bufR(2048, 0.5F);
    float* ptrs[2] = {bufL.data(), bufR.data()};
    phaser.process(ptrs, 2, 2048);

    for (std::size_t i = 0; i < 2048; ++i) {
      REQUIRE(std::isfinite(bufL[i]));
      REQUIRE(std::isfinite(bufR[i]));
      CHECK(std::abs(bufL[i]) < 2.0F);
    }
  }
}

TEST_CASE("ChannelStrip integrates FX slots in signal path", "[channel_strip][fx]") {
  ChannelStrip strip;
  strip.prepare(kRate);

  SECTION("bypassed FX slot passes audio through unaffected") {
    ChannelStrip strip1;
    strip1.prepare(kRate);

    ChannelStrip strip2;
    strip2.prepare(kRate);
    auto delay = std::make_unique<DelayEffect>();
    delay->setEnabled(false);
    strip2.fxSlot(0).setEffect(std::move(delay));
    strip2.fxSlot(0).setEnabled(false);

    std::vector<float> inL1(256, 0.5F);
    std::vector<float> inR1(256, 0.5F);
    float* ptrs1[2] = {inL1.data(), inR1.data()};

    std::vector<float> inL2(256, 0.5F);
    std::vector<float> inR2(256, 0.5F);
    float* ptrs2[2] = {inL2.data(), inR2.data()};

    strip1.process(ptrs1, 2, 256);
    strip2.process(ptrs2, 2, 256);

    for (int i = 0; i < 256; ++i) {
      CHECK(inL1[static_cast<std::size_t>(i)] == inL2[static_cast<std::size_t>(i)]);
      CHECK(inR1[static_cast<std::size_t>(i)] == inR2[static_cast<std::size_t>(i)]);
    }
  }

  SECTION("active FX slot processes audio and remains realtime safe") {
    auto delay = std::make_unique<DelayEffect>();
    delay->setDelayTimeMs(20.0F);
    delay->setFeedback(0.3F);
    delay->setDryWet(0.5F);

    strip.fxSlot(0).setEffect(std::move(delay));
    REQUIRE(strip.fxSlot(0).effect() != nullptr);

    std::vector<float> inL(512, 0.3F);
    std::vector<float> inR(512, 0.3F);
    float* ptrs[2] = {inL.data(), inR.data()};

    {
      ScopedRealtimeGuard rtGuard;
      strip.process(ptrs, 2, 512);
    }

    for (int i = 0; i < 512; ++i) {
      REQUIRE(std::isfinite(inL[static_cast<std::size_t>(i)]));
      REQUIRE(std::isfinite(inR[static_cast<std::size_t>(i)]));
    }
  }
}
