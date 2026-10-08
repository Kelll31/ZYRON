// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "Audio/DSP/OutputStage.hpp"
#include "Audio/DSP/TestToneGenerator.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron::test;

TEST_CASE("ScopedRealtimeGuard detects heap allocations and deallocations") {
  SECTION("a clean scope reports zero allocations and zero deallocations") {
    ScopedRealtimeGuard guard;
    volatile int x = 42;
    (void)x;
    REQUIRE_FALSE(guard.hasViolations());
    REQUIRE(guard.allocationCount() == 0);
    REQUIRE(guard.allocatedBytes() == 0);
    REQUIRE(guard.deallocationCount() == 0);
  }

  SECTION("new triggers allocation violation") {
    ScopedRealtimeGuard guard;
    int* ptr = new int(123);
    REQUIRE(guard.hasViolations());
    REQUIRE(guard.allocationCount() >= 1);
    REQUIRE(guard.allocatedBytes() >= sizeof(int));
    delete ptr;
  }

  SECTION("array new triggers allocation violation") {
    ScopedRealtimeGuard guard;
    char* buf = new char[64];
    REQUIRE(guard.hasViolations());
    REQUIRE(guard.allocationCount() >= 1);
    REQUIRE(guard.allocatedBytes() >= 64);
    delete[] buf;
  }

  SECTION("std::vector allocation triggers violation") {
    ScopedRealtimeGuard guard;
    std::vector<int> v;
    v.reserve(100);
    REQUIRE(guard.hasViolations());
    REQUIRE(guard.allocationCount() >= 1);
  }

  SECTION("delete triggers deallocation violation") {
    int* ptr = nullptr;
    {
      ScopedGuardBypass bypass;
      ptr = new int(999);
    }

    ScopedRealtimeGuard guard;
    delete ptr;
    REQUIRE(guard.hasViolations());
    REQUIRE(guard.deallocationCount() >= 1);
  }

  SECTION("ScopedGuardBypass allows allocations without violation") {
    ScopedRealtimeGuard guard;
    {
      ScopedGuardBypass bypass;
      std::vector<double> v(50, 1.0);
      REQUIRE(v.size() == 50);
    }
    REQUIRE_FALSE(guard.hasViolations());
  }

  SECTION("guard state is thread-local") {
    ScopedRealtimeGuard mainGuard;

    std::thread worker;
    {
      ScopedGuardBypass bypass;
      worker = std::thread([&]() {
        // Worker thread does not have guard active; its allocation should not affect mainGuard
        std::vector<int> workerVec(10, 5);
        (void)workerVec;
      });
    }

    worker.join();
    // Main thread guard saw no allocations on main thread
    REQUIRE_FALSE(mainGuard.hasViolations());
  }
}

TEST_CASE("TestToneGenerator and OutputStage render are real-time safe (zero allocations)") {
  zyron::audio::TestToneGenerator tone;
  tone.prepare(48000.0);
  tone.setFrequencyHz(1000.0F);
  tone.setLevelDb(0.0F);
  tone.setEnabled(true);

  std::vector<float> buffer(512, 0.0F);

  SECTION("TestToneGenerator::render does not allocate or deallocate") {
    ScopedRealtimeGuard guard;
    tone.render(buffer.data(), 512);
    REQUIRE_FALSE(guard.hasViolations());
  }

  SECTION("OutputStage::render does not allocate or deallocate") {
    zyron::audio::OutputStage stage;
    stage.prepare(48000.0);
    stage.tone().setEnabled(true);

    float* channels[2] = {buffer.data(), buffer.data()};
    ScopedRealtimeGuard guard;
    stage.render(channels, 2, 512);
    REQUIRE_FALSE(guard.hasViolations());
  }
}

TEST_CASE("Synthetic audio signal generators produce expected signals") {
  constexpr double kRate = 48000.0;

  SECTION("generateSine produces bounded sine with correct magnitude") {
    const auto sine = generateSine(kRate, 1000.0, 0.1, 0.8F);
    REQUIRE(sine.size() == 4800);
    REQUIRE(isFinite(sine));
    REQUIRE(isWithinClip(sine, 0.81F));
    const float mag = measureMagnitudeAt(sine, kRate, 1000.0);
    REQUIRE_THAT(mag, Catch::Matchers::WithinAbs(0.8F, 0.01F));
  }

  SECTION("generateImpulse produces isolated delta") {
    const auto imp = generateImpulse(100, 25, 0.5F);
    REQUIRE(imp.size() == 100);
    REQUIRE(imp[25] == 0.5F);
    REQUIRE(imp[0] == 0.0F);
    REQUIRE(imp[99] == 0.0F);
  }

  SECTION("generateNoise is deterministic with same seed and bounded") {
    const auto n1 = generateNoise(1000, 0.5F, 12345);
    const auto n2 = generateNoise(1000, 0.5F, 12345);
    const auto n3 = generateNoise(1000, 0.5F, 99999);
    REQUIRE(n1 == n2);
    REQUIRE(n1 != n3);
    REQUIRE(isWithinClip(n1, 0.5F));
  }

  SECTION("generateDc produces constant value") {
    const auto dc = generateDc(200, 0.75F);
    REQUIRE(dc.size() == 200);
    for (float s : dc) {
      REQUIRE(s == 0.75F);
    }
  }

  SECTION("generateClickTrack produces periodic bursts") {
    const auto clicks = generateClickTrack(kRate, 120.0, 1.5, 0.0);
    REQUIRE(clicks.size() == static_cast<std::size_t>(kRate * 1.5));
    REQUIRE(isFinite(clicks));
    // At 120 BPM, interval is 0.5 s: clicks at 0.0 s, 0.5 s, 1.0 s -> peak amplitudes near these
    REQUIRE(maxAbsolute(clicks, 0) > 0.5F);
  }

  SECTION("generateSweep produces bounded chirp") {
    const auto sweep = generateSweep(kRate, 100.0, 10000.0, 0.2, 0.9F);
    REQUIRE(sweep.size() == static_cast<std::size_t>(kRate * 0.2));
    REQUIRE(isFinite(sweep));
    REQUIRE(isWithinClip(sweep, 0.91F));
  }
}

TEST_CASE("DSP verification probes and analysis functions") {
  SECTION("isFinite detects NaN and Infinity") {
    std::vector<float> valid = {0.0F, 1.0F, -0.5F};
    REQUIRE(isFinite(valid));

    valid[1] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(isFinite(valid));

    valid[1] = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(isFinite(valid));
  }

  SECTION("nullTestMaxDiff computes exact differences") {
    std::vector<float> a = {1.0F, 2.0F, 3.0F};
    std::vector<float> b = {1.0F, 2.05F, 3.0F};
    REQUIRE_THAT(nullTestMaxDiff(a, b), Catch::Matchers::WithinAbs(0.05F, 0.0001F));

    std::vector<float> c = {1.0F};
    REQUIRE(std::isinf(nullTestMaxDiff(a, c)));
  }

  SECTION("crossCorrelation accurately finds delay lag") {
    const auto original = generateNoise(2000, 1.0F, 42);
    // Create shifted signal delayed by 15 samples
    std::vector<float> delayed(2000, 0.0F);
    for (std::size_t i = 15; i < 2000; ++i) {
      delayed[i] = original[i - 15];
    }

    const auto result = crossCorrelation(original, delayed, 50);
    REQUIRE(result.bestLag == 15);
    REQUIRE(result.maxCorrelation > 0.95F);
  }
}

TEST_CASE("Offline rendering and block size independence") {
  constexpr double kRate = 48000.0;
  constexpr int kTotalSamples = 48000;  // 1 second

  // TestToneGenerator is designed to be block-size independent
  auto makeGenerator = []() {
    auto tone = std::make_unique<zyron::audio::TestToneGenerator>();
    tone->prepare(kRate);
    tone->setFrequencyHz(440.0F);
    tone->setLevelDb(0.0F);
    tone->setEnabled(true);
    return [t = std::move(tone)](float* out, int n) mutable { t->render(out, n); };
  };

  const float maxDiff = testBlockSizeIndependence(makeGenerator, kTotalSamples, standardTestBlockSizes(), 128);
  // Settled sine should have negligible difference across all block sizes
  REQUIRE(maxDiff < 1e-4F);
}
