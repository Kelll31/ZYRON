// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

#include "Audio/DSP/TestToneGenerator.hpp"
#include "support/ThreadGroup.hpp"

#include "support/AllocationGuard.hpp"

using zyron::audio::TestToneGenerator;

namespace {

constexpr double kSampleRate = 48000.0;

std::vector<float> render(TestToneGenerator& tone, int totalSamples, int blockSize) {
  std::vector<float> out(static_cast<std::size_t>(totalSamples), 99.0F);  // 99 = "was not written"
  for (int done = 0; done < totalSamples;) {
    const int n = std::min(blockSize, totalSamples - done);
    {
      zyron::test::ScopedRealtimeGuard guard;
      tone.render(out.data() + done, n);
      REQUIRE_FALSE(guard.hasViolations());
    }
    done += n;
  }
  return out;
}

float maxAbs(const std::vector<float>& v, std::size_t from = 0) {
  float peak = 0.0F;
  for (std::size_t i = from; i < v.size(); ++i) {
    peak = std::max(peak, std::fabs(v[i]));
  }
  return peak;
}

float maxStep(const std::vector<float>& v, std::size_t from = 1) {
  float step = 0.0F;
  for (std::size_t i = std::max<std::size_t>(from, 1); i < v.size(); ++i) {
    step = std::max(step, std::fabs(v[i] - v[i - 1]));
  }
  return step;
}

int zeroCrossings(const std::vector<float>& v, std::size_t from) {
  int count = 0;
  for (std::size_t i = from + 1; i < v.size(); ++i) {
    if ((v[i - 1] < 0.0F) != (v[i] < 0.0F)) {
      ++count;
    }
  }
  return count;
}

/// Configures a generator in place (it holds atomics, so it cannot be returned by value).
void setup(TestToneGenerator& tone, bool enabled, float frequencyHz, float levelDb) {
  tone.prepare(kSampleRate);
  tone.setFrequencyHz(frequencyHz);
  tone.setLevelDb(levelDb);
  tone.setEnabled(enabled);
}

}  // namespace

TEST_CASE("a disabled tone is exact silence") {
  TestToneGenerator tone;
  setup(tone, false, 1000.0F, 0.0F);

  const auto out = render(tone, 4800, 128);

  for (const float sample : out) {
    REQUIRE(sample == 0.0F);
  }
  CHECK(tone.isSilent());
}

TEST_CASE("an unprepared generator writes silence instead of garbage") {
  TestToneGenerator tone;
  tone.setEnabled(true);

  const auto out = render(tone, 256, 64);

  CHECK(maxAbs(out) == 0.0F);
}

TEST_CASE("the tone has the requested frequency and level once settled") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);

  const auto out = render(tone, 48000, 256);
  const std::size_t settled = 4800;  // skip 100 ms of fade-in

  // 1 s settled to ~0.9 s of a 1 kHz sine: about 1800 zero crossings.
  CHECK(std::abs(zeroCrossings(out, settled) - 1800) <= 3);
  CHECK_THAT(maxAbs(out, settled), Catch::Matchers::WithinAbs(1.0, 0.002));
}

TEST_CASE("the level is applied in dB") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, -6.0F);

  const auto out = render(tone, 24000, 128);

  CHECK_THAT(maxAbs(out, 4800), Catch::Matchers::WithinAbs(std::pow(10.0, -6.0 / 20.0), 0.002));
}

/// Renders `before`, applies `change`, renders `after`, and returns the largest sample-to-sample step across the whole
/// stream - including the seam where the parameter changed, which is where a click would be.
template <class Change>
float maxStepAcrossChange(TestToneGenerator& tone, int beforeSamples, Change&& change, int afterSamples) {
  std::vector<float> stream = render(tone, beforeSamples, 128);
  change();
  const std::vector<float> after = render(tone, afterSamples, 128);
  stream.insert(stream.end(), after.begin(), after.end());
  return maxStep(stream, stream.size() - after.size());  // steps starting at the seam
}

// A 1 kHz tone at 48 kHz has a 48-sample period, so after 12 + 48*k samples the next sample is the peak of the sine.
// Changing a parameter exactly there is the worst case: a hard change would jump by almost the full amplitude.
constexpr int kSamplesToPeak = 12 + 48 * 200;  // 9612

TEST_CASE("switching the tone on at the peak of the sine does not click") {
  TestToneGenerator tone;
  setup(tone, false, 1000.0F, 0.0F);

  // The phase keeps running while the tone is off, so enabling at 60 samples starts right at the peak.
  const float step = maxStepAcrossChange(tone, 12, [&] { tone.setEnabled(true); }, 4800);

  // A steady 1 kHz sine at full scale moves at most 2*pi*f/fs = 0.131 per sample; the fade adds at most ~0.005.
  // A hard start (the click we are preventing) would jump by about 1.0.
  CHECK(step < 0.15F);
}

TEST_CASE("switching the tone off at the peak of the sine fades to exact silence without a step") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);

  std::vector<float> tail;
  const float step = maxStepAcrossChange(tone, kSamplesToPeak, [&] { tone.setEnabled(false); }, 9600);
  CHECK(step < 0.15F);

  tail = render(tone, 256, 64);
  CHECK(maxAbs(tail) == 0.0F);  // after 200 ms the tone is gone and the tail is exactly zero, not a denormal trickle
  CHECK(tone.isSilent());
}

TEST_CASE("the fade-out reaches silence within 100 ms") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);
  (void)render(tone, kSamplesToPeak, 128);
  tone.setEnabled(false);

  const auto out = render(tone, 9600, 128);

  CHECK(maxAbs(out, 4800) < 1e-4F);
  CHECK(out.back() == 0.0F);
}

TEST_CASE("changing the level at the peak of the sine does not click") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);

  const float step = maxStepAcrossChange(tone, kSamplesToPeak, [&] { tone.setLevelDb(-40.0F); }, 4800);

  CHECK(step < 0.15F);
}

TEST_CASE("changing the frequency keeps the phase continuous") {
  TestToneGenerator tone;
  setup(tone, true, 440.0F, 0.0F);
  // 9637 samples is deliberately not a whole number of periods, so the switch happens at an arbitrary phase.
  auto first = render(tone, 9637, 100);
  tone.setFrequencyHz(880.0F);
  const auto second = render(tone, 4800, 100);

  first.insert(first.end(), second.begin(), second.end());

  // After the switch the sine runs at 880 Hz (max step 2*pi*880/48000 = 0.115); a phase jump would exceed that.
  CHECK(maxStep(first, 9600) < 0.13F);
}

TEST_CASE("the output does not depend on the block size") {
  const std::vector<int> blockSizes{1, 7, 32, 64, 480, 1000, 4800};
  TestToneGenerator reference;
  setup(reference, true, 1234.5F, -3.0F);
  const auto expected = render(reference, 9600, 4800);

  for (const int blockSize : blockSizes) {
    INFO("block size " << blockSize);
    TestToneGenerator tone;
    setup(tone, true, 1234.5F, -3.0F);
    CHECK(render(tone, 9600, blockSize) == expected);
  }
}

TEST_CASE("out-of-range and non-finite parameters are clamped, never producing NaN") {
  constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
  constexpr float kInf = std::numeric_limits<float>::infinity();

  TestToneGenerator loud;
  setup(loud, true, 1000.0F, 20.0F);  // +20 dB is clamped to 0 dB
  CHECK(maxAbs(render(loud, 9600, 256), 4800) <= 1.001F);

  for (const float badFrequency : {kNaN, kInf, -kInf, 0.0F, -50.0F, 1.0e9F}) {
    INFO("frequency " << badFrequency);
    TestToneGenerator tone;
    setup(tone, true, badFrequency, -10.0F);
    const auto out = render(tone, 4800, 256);
    CHECK(std::all_of(out.begin(), out.end(), [](float s) { return std::isfinite(s); }));
    CHECK(maxAbs(out) > 0.0F);
  }

  TestToneGenerator badLevel;

  setup(badLevel, true, 1000.0F, kNaN);
  const auto out = render(badLevel, 4800, 256);
  CHECK(std::all_of(out.begin(), out.end(), [](float s) { return std::isfinite(s); }));
}

TEST_CASE("prepare resets a running tone to silence") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);
  (void)render(tone, 9600, 128);

  tone.prepare(44100.0);
  tone.setEnabled(false);

  CHECK(maxAbs(render(tone, 256, 64)) == 0.0F);
}

TEST_CASE("the fade-in follows the 5 ms time constant exactly") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);

  (void)render(tone, 240, 64);  // 5 ms at 48 kHz = one time constant
  CHECK_THAT(tone.gain(), Catch::Matchers::WithinAbs(1.0 - std::exp(-1.0), 1e-6));

  (void)render(tone, 720, 64);  // 20 ms in total = four time constants
  CHECK_THAT(tone.gain(), Catch::Matchers::WithinAbs(1.0 - std::exp(-4.0), 1e-6));
}

TEST_CASE("the fade-out follows the same time constant") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);
  (void)render(tone, 48000, 256);  // fully settled
  tone.setEnabled(false);

  (void)render(tone, 240, 64);

  CHECK_THAT(tone.gain(), Catch::Matchers::WithinAbs(std::exp(-1.0), 1e-5));
}

TEST_CASE("the gain settles at the requested level") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, -6.0F);

  (void)render(tone, 48000, 256);

  CHECK_THAT(tone.gain(), Catch::Matchers::WithinAbs(std::pow(10.0, -6.0 / 20.0), 1e-4));
}

TEST_CASE("suspending fades the tone out without touching the user's settings") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);
  CHECK(tone.isFadedOut());  // nothing has played yet

  const float step = maxStepAcrossChange(tone, kSamplesToPeak, [&] { tone.setSuspended(true); }, 9600);

  CHECK(step < 0.15F);       // stopped at the peak of the sine, still no click
  CHECK(tone.isFadedOut());  // published for the message thread to poll
  CHECK(tone.enabled());     // the user's switch is untouched
  CHECK(maxAbs(render(tone, 256, 64)) == 0.0F);

  tone.setSuspended(false);
  const auto resumed = render(tone, 9600, 128);
  CHECK(maxAbs(resumed, 4800) > 0.99F);  // and the tone comes back on its own
  CHECK_FALSE(tone.isFadedOut());
}

TEST_CASE("isFadedOut turns true only after the fade-out has really finished") {
  TestToneGenerator tone;
  setup(tone, true, 1000.0F, 0.0F);
  (void)render(tone, 9600, 128);
  CHECK_FALSE(tone.isFadedOut());

  tone.setSuspended(true);
  (void)render(tone, 240, 64);  // one time constant: gain is still 37 %
  CHECK_FALSE(tone.isFadedOut());

  (void)render(tone, 4800, 64);  // 100 ms: far below the -80 dB threshold
  CHECK(tone.isFadedOut());
}

TEST_CASE("the setters are independent: changing one never disturbs the others") {
  TestToneGenerator tone;
  setup(tone, false, 440.0F, 0.0F);

  tone.setFrequencyHz(2000.0F);
  tone.setLevelDb(-10.0F);
  tone.setEnabled(true);
  tone.setSuspended(true);
  tone.setSuspended(false);
  const auto out = render(tone, 48000, 256);

  CHECK(tone.enabled());
  CHECK_THAT(maxAbs(out, 9600), Catch::Matchers::WithinAbs(std::pow(10.0, -10.0 / 20.0), 0.002));
  CHECK(std::abs(zeroCrossings(out, 9600) - 2 * 2000 * 38400 / 48000) <= 6);  // 2000 Hz over the settled 0.8 s
}

TEST_CASE("the frequency is limited below Nyquist so a high tone cannot alias on a low-rate device") {
  TestToneGenerator tone;
  tone.prepare(16000.0);  // e.g. a Bluetooth headset profile
  tone.setLevelDb(0.0F);
  tone.setFrequencyHz(20000.0F);  // above Nyquist for this device
  tone.setEnabled(true);

  const auto out = render(tone, 16000, 256);

  // Clamped to 0.45 * 16000 = 7200 Hz: about 2 * 7200 zero crossings per second. Unclamped, it would alias to 4000 Hz
  // (about 8000 crossings).
  CHECK(std::abs(zeroCrossings(out, 1600) - 2 * 7200 * 14400 / 16000) <= 40);
}

TEST_CASE("parameters can be changed from other threads while the audio thread renders") {
  TestToneGenerator tone;
  tone.prepare(kSampleRate);

  std::atomic<bool> stop{false};
  zyron::test::ThreadGroup controllers;
  struct StopControllers {
    std::atomic<bool>& stop;
    ~StopControllers() { stop.store(true); }
  } stopControllers{stop};
  for (int t = 0; t < 3; ++t) {
    controllers.spawn([&, t] {
      float x = static_cast<float>(t);
      while (!stop.load()) {
        tone.setEnabled((static_cast<int>(x) & 1) == 0);
        tone.setSuspended((static_cast<int>(x) % 7) == 0);
        tone.setFrequencyHz(20.0F + std::fmod(x * 37.0F, 19000.0F));
        tone.setLevelDb(-96.0F + std::fmod(x * 3.0F, 96.0F));
        x += 1.0F;
      }
    });
  }

  bool allFinite = true;
  for (int block = 0; block < 2000; ++block) {
    std::vector<float> out(64);
    tone.render(out.data(), 64);
    allFinite = allFinite &&
                std::all_of(out.begin(), out.end(), [](float s) { return std::isfinite(s) && std::fabs(s) <= 1.001F; });
  }
  stop.store(true);
  controllers.joinAll();

  CHECK(allFinite);
}
