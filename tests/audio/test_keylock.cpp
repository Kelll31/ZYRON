// SPDX-License-Identifier: AGPL-3.0-only
// Keylock (master tempo) and key shift through the time-stretcher (ADR-0018). Offline renders of synthetic signals.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

#include "Audio/DSP/TimeStretcher.hpp"
#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron::audio;
using namespace zyron::test;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = std::numbers::pi;

std::shared_ptr<TrackBuffer> makeTrack(const std::vector<float>& mono, double sampleRate = kRate) {
  auto track = std::make_shared<TrackBuffer>(2, static_cast<std::int64_t>(mono.size()), sampleRate);
  std::copy(mono.begin(), mono.end(), track->channelData(0));
  std::copy(mono.begin(), mono.end(), track->channelData(1));
  return track;
}

std::shared_ptr<TrackBuffer> sineTrack(double freq, double seconds, float amp = 0.5F, double sampleRate = kRate) {
  return makeTrack(generateSine(sampleRate, freq, seconds, amp), sampleRate);
}

/// Renders `samples` frames of the left channel in blocks of `blockSize`.
std::vector<float> renderLeft(DeckPlayer& deck, int samples, int blockSize) {
  std::vector<float> out(static_cast<std::size_t>(samples));
  std::vector<float> right(static_cast<std::size_t>(blockSize));
  for (int done = 0; done < samples; done += blockSize) {
    const int n = std::min(blockSize, samples - done);
    float* planes[2] = {out.data() + done, right.data()};
    deck.render(planes, 2, n);
  }
  return out;
}

/// Frequency from upward zero crossings (linear interpolation) in [from, to). Needs a clean, roughly sinusoidal signal.
double estimateFrequency(const std::vector<float>& x, std::size_t from, std::size_t to, double rate) {
  double first = -1.0;
  double last = -1.0;
  int crossings = 0;
  for (std::size_t i = std::max<std::size_t>(from, 1); i < to; ++i) {
    if (x[i - 1] < 0.0F && x[i] >= 0.0F) {
      const double t = static_cast<double>(i - 1) + static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
      if (first < 0.0) {
        first = t;
      }
      last = t;
      ++crossings;
    }
  }
  if (crossings < 3) {
    return 0.0;
  }
  return static_cast<double>(crossings - 1) / ((last - first) / rate);
}

/// Largest sample-to-sample step. A sine of amplitude 0.5 below 1.2 kHz stays under 0.08; an uncorrected click is bigger.
float maxFirstDifference(const std::vector<float>& x, std::size_t from = 1) {
  float worst = 0.0F;
  for (std::size_t i = std::max<std::size_t>(from, 1); i < x.size(); ++i) {
    worst = std::max(worst, std::abs(x[i] - x[i - 1]));
  }
  return worst;
}

/// Declicked jumps hold the last value for one sample and then fade to the new signal (3 ms): no step, but a visible
/// kink, so the bounds are "far below a real click" rather than "as smooth as the sine".
constexpr float kMaxStep = 0.12F;
constexpr float kMaxKink = 0.2F;

/// Largest second difference: a sine of this frequency and amplitude never exceeds amp * (2 pi f / rate)^2.
float maxSecondDifference(const std::vector<float>& x, std::size_t from = 2) {
  float worst = 0.0F;
  for (std::size_t i = std::max<std::size_t>(from, 2); i < x.size(); ++i) {
    worst = std::max(worst, std::abs(x[i] - 2.0F * x[i - 1] + x[i - 2]));
  }
  return worst;
}

/// Energy centroid (seconds) of x^2 in [from, to).
double centroidSeconds(const std::vector<float>& x, std::size_t from, std::size_t to, double rate) {
  double weighted = 0.0;
  double total = 0.0;
  for (std::size_t i = from; i < to && i < x.size(); ++i) {
    const double e = static_cast<double>(x[i]) * static_cast<double>(x[i]);
    weighted += e * static_cast<double>(i);
    total += e;
  }
  return total > 0.0 ? weighted / total / rate : -1.0;
}

}  // namespace

TEST_CASE("Keylock: a sine keeps its pitch at +6 % tempo, varispeed does not", "[audio][keylock]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(1000.0, 12.0));
  deck.setPlaybackSpeed(1.06);
  deck.play();
  REQUIRE(deck.keylockEnabled());  // on by default

  const auto out = renderLeft(deck, static_cast<int>(3.0 * kRate), 256);
  CHECK(deck.isStretching());
  const double f = estimateFrequency(out, static_cast<std::size_t>(0.5 * kRate), out.size(), kRate);
  CHECK(std::abs(f - 1000.0) / 1000.0 < 0.01);
  CHECK(isFinite(out));

  // The tempo itself is right: the playhead moved 6 % faster than real time.
  CHECK(deck.currentTimeSec() == Catch::Approx(3.0 * 1.06).margin(0.01));

  SECTION("keylock off is plain varispeed: pitch follows the tempo") {
    DeckPlayer record;
    record.prepare(kRate);
    record.loadTrack(sineTrack(1000.0, 12.0));
    record.setKeylock(false);
    record.setPlaybackSpeed(1.06);
    record.play();
    const auto v = renderLeft(record, static_cast<int>(2.0 * kRate), 256);
    CHECK_FALSE(record.isStretching());
    const double fv = estimateFrequency(v, static_cast<std::size_t>(0.5 * kRate), v.size(), kRate);
    CHECK(std::abs(fv - 1060.0) / 1060.0 < 0.005);
  }
}

TEST_CASE("Keylock: slower and faster tempos keep the pitch too", "[audio][keylock]") {
  for (const double speed : {0.92, 0.97, 1.03, 1.12, 1.5}) {
    DeckPlayer deck;
    deck.prepare(kRate);
    deck.loadTrack(sineTrack(440.0, 12.0));
    deck.setPlaybackSpeed(speed);
    deck.play();
    const auto out = renderLeft(deck, static_cast<int>(2.5 * kRate), 512);
    const double f = estimateFrequency(out, static_cast<std::size_t>(0.5 * kRate), out.size(), kRate);
    INFO("speed " << speed << " -> " << f << " Hz");
    CHECK(std::abs(f - 440.0) / 440.0 < 0.01);
  }
}

TEST_CASE("Key shift: +2 semitones moves the pitch by 12.2 %, -2 by 10.9 %", "[audio][keylock][keyshift]") {
  for (const float semitones : {2.0F, -2.0F, 5.0F}) {
    DeckPlayer deck;
    deck.prepare(kRate);
    deck.loadTrack(sineTrack(800.0, 12.0));
    deck.setKeyShift(semitones);
    deck.play();
    const auto out = renderLeft(deck, static_cast<int>(2.5 * kRate), 256);
    CHECK(deck.isStretching());
    const double expected = 800.0 * std::pow(2.0, static_cast<double>(semitones) / 12.0);
    const double f = estimateFrequency(out, static_cast<std::size_t>(0.6 * kRate), out.size(), kRate);
    INFO("shift " << semitones << " -> " << f << " Hz, expected " << expected);
    CHECK(std::abs(f - expected) / expected < 0.01);
    CHECK(deck.currentTimeSec() == Catch::Approx(2.5).margin(0.01));  // tempo untouched
  }
}

TEST_CASE("Key shift works with keylock off (pitch = varispeed + shift) and stays in range", "[audio][keylock][keyshift]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(800.0, 12.0));
  deck.setKeylock(false);
  deck.setPlaybackSpeed(1.05);
  deck.setKeyShift(3.0F);
  deck.play();
  const auto out = renderLeft(deck, static_cast<int>(2.5 * kRate), 256);
  const double expected = 800.0 * 1.05 * std::pow(2.0, 3.0 / 12.0);
  const double f = estimateFrequency(out, static_cast<std::size_t>(0.6 * kRate), out.size(), kRate);
  CHECK(std::abs(f - expected) / expected < 0.012);

  deck.setKeyShift(100.0F);
  CHECK(deck.keyShift() == 12.0F);
  deck.setKeyShift(std::nanf(""));
  CHECK(deck.keyShift() == 12.0F);  // non-finite values are ignored
}

TEST_CASE("Keylock: an untouched deck at 1.0x bypasses the stretcher and is bit exact", "[audio][keylock]") {
  const auto source = generateSine(kRate, 700.0, 4.0, 0.5F);
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(makeTrack(source));
  deck.play();
  const auto out = renderLeft(deck, static_cast<int>(1.0 * kRate), 128);
  CHECK_FALSE(deck.isStretching());
  // After the 5 ms fade-in the output is the source itself.
  CHECK(nullTestMaxDiff({out.begin() + 4800, out.begin() + 9600}, {source.begin() + 4800, source.begin() + 9600}) < 1e-5F);
}

TEST_CASE("Keylock: output does not depend on the block size", "[audio][keylock]") {
  const auto source = generateSweep(kRate, 200.0, 4000.0, 6.0, 0.4F);
  const float worst = testBlockSizeIndependence(
      [&] {
        auto deck = std::make_shared<DeckPlayer>();
        deck->prepare(kRate);
        deck->loadTrack(makeTrack(source));
        deck->setPlaybackSpeed(1.07);
        deck->setKeyShift(1.5F);
        deck->play();
        return [deck](float* out, int n) {
          std::vector<float> right(static_cast<std::size_t>(n));
          float* planes[2] = {out, right.data()};
          deck->render(planes, 2, n);
        };
      },
      static_cast<int>(1.5 * kRate), {32, 64, 100, 256, 480, 1024, 2048}, 128);
  CHECK(worst < 1e-6F);
}

TEST_CASE("Keylock: the stretcher latency is compensated, clicks land where the playhead says", "[audio][keylock]") {
  // A click every half second, 5 ms bursts.
  const double bpm = 120.0;
  const auto clicks = generateClickTrack(kRate, bpm, 14.0, 0.25);
  for (const double speed : {1.06, 0.94, 1.25}) {
    DeckPlayer deck;
    deck.prepare(kRate);
    deck.loadTrack(makeTrack(clicks));
    deck.setPlaybackSpeed(speed);
    deck.play();
    const int total = static_cast<int>(6.0 * kRate);
    const auto out = renderLeft(deck, total, 256);
    CHECK(deck.isStretching());

    double worstMs = 0.0;
    for (int k = 2; k < 9; ++k) {
      const double clickTime = 0.25 + 0.5 * k + 0.0025;  // burst centre in the source (s)
      const double expected = clickTime / speed;           // when it sounds (s)
      const auto from = static_cast<std::size_t>(std::max(0.0, expected - 0.12) * kRate);
      const auto to = static_cast<std::size_t>((expected + 0.12) * kRate);
      if (to >= out.size()) {
        break;
      }
      const double heard = centroidSeconds(out, from, to, kRate);
      REQUIRE(heard > 0.0);
      worstMs = std::max(worstMs, std::abs(heard - expected) * 1000.0);
    }
    INFO("speed " << speed << ": worst click misalignment " << worstMs << " ms");
    std::printf("[bench] keylock click alignment at %.2fx: worst %.2f ms\n", speed, worstMs);
    CHECK(worstMs < 1.0);
  }
}

TEST_CASE("Keylock: a seek while playing restarts on the new position without a gap", "[audio][keylock]") {
  const auto clicks = generateClickTrack(kRate, 120.0, 20.0, 0.0);
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(makeTrack(clicks));
  deck.setPlaybackSpeed(1.08);
  deck.play();
  (void)renderLeft(deck, static_cast<int>(1.0 * kRate), 256);

  deck.seekSeconds(8.0);  // exactly on a beat: the click sounds right at the start of the next block
  const auto out = renderLeft(deck, static_cast<int>(1.5 * kRate), 256);
  const double expected = (0.0 + 0.0025) / 1.08;  // first click after the seek, relative to the seek
  const double heard = centroidSeconds(out, 0, static_cast<std::size_t>(0.2 * kRate), kRate);
  CHECK(std::abs(heard - expected) * 1000.0 < 1.5);
  CHECK(isFinite(out));
  CHECK(deck.currentTimeSec() == Catch::Approx(8.0 + 1.5 * 1.08).margin(0.02));
}

TEST_CASE("Keylock: toggling keylock and key shift while playing never clicks or produces NaN", "[audio][keylock]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(1000.0, 30.0, 0.5F));
  deck.setPlaybackSpeed(1.0);
  deck.play();

  std::vector<float> all;
  const auto append = [&](int frames) {
    const auto part = renderLeft(deck, frames, 256);
    all.insert(all.end(), part.begin(), part.end());
  };
  append(static_cast<int>(0.5 * kRate));
  deck.setPlaybackSpeed(1.08);  // 1.0 -> 1.08 with keylock on: varispeed bypass -> stretcher
  append(static_cast<int>(0.7 * kRate));
  deck.setKeylock(false);       // stretcher -> varispeed
  append(static_cast<int>(0.5 * kRate));
  deck.setKeylock(true);
  append(static_cast<int>(0.5 * kRate));
  deck.setKeyShift(2.0F);
  append(static_cast<int>(0.5 * kRate));
  deck.setKeyShift(-4.0F);
  append(static_cast<int>(0.5 * kRate));
  deck.setKeyShift(0.0F);
  deck.setPlaybackSpeed(1.0);   // stretcher -> bypass
  append(static_cast<int>(0.7 * kRate));

  CHECK(isFinite(all));
  CHECK(maxAbsolute(all) < 0.7F);
  CHECK(maxFirstDifference(all, 4800) < kMaxStep);
  CHECK(maxSecondDifference(all, 4800) < kMaxKink);
}

TEST_CASE("Keylock: loops wrap cleanly and a loop roll bypasses the stretcher", "[audio][keylock]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(500.0, 30.0, 0.5F));
  deck.setPlaybackSpeed(1.06);
  deck.play();
  (void)renderLeft(deck, static_cast<int>(0.5 * kRate), 256);

  SECTION("a one second loop stays stretched, wraps and stays inside the loop") {
    deck.setLoopSeconds(2.0, 3.0, true);
    deck.seekSeconds(1.9);
    std::vector<float> all;
    for (int i = 0; i < 4; ++i) {
      const auto part = renderLeft(deck, static_cast<int>(0.75 * kRate), 256);
      all.insert(all.end(), part.begin(), part.end());
    }
    CHECK(deck.isStretching());
    CHECK(isFinite(all));
    CHECK(maxFirstDifference(all, 2000) < kMaxStep);
    CHECK(maxSecondDifference(all, 2000) < kMaxKink);
    CHECK(deck.currentTimeSec() >= 2.0);
    CHECK(deck.currentTimeSec() <= 3.0);
  }
  SECTION("a 1/8 beat roll plays varispeed") {
    deck.setLoopSeconds(2.0, 2.0625, true);
    deck.seekSeconds(2.0);
    const auto out = renderLeft(deck, static_cast<int>(0.5 * kRate), 128);
    CHECK_FALSE(deck.isStretching());
    CHECK(isFinite(out));
    CHECK(maxFirstDifference(out, 2000) < kMaxStep);
    CHECK(maxSecondDifference(out, 2000) < kMaxKink);
    deck.setLoopActive(false);  // releasing the roll resumes the stretched, pitch-locked playback
    const auto after = renderLeft(deck, static_cast<int>(1.5 * kRate), 128);
    CHECK(deck.isStretching());
    CHECK(maxFirstDifference(after) < kMaxStep);
    CHECK(maxSecondDifference(after, 0) < kMaxKink);
    const double f = estimateFrequency(after, static_cast<std::size_t>(1.0 * kRate), after.size(), kRate);
    CHECK(std::abs(f - 500.0) / 500.0 < 0.01);
  }
}

TEST_CASE("Keylock: scratches and brakes bypass the stretcher and playback resumes cleanly", "[audio][keylock]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(600.0, 30.0, 0.5F));
  deck.setPlaybackSpeed(1.06);
  deck.play();
  (void)renderLeft(deck, static_cast<int>(1.0 * kRate), 256);
  CHECK(deck.isStretching());

  deck.startScratch(0 /* Baby */, 1.0, 0.5);
  std::vector<float> scratch = renderLeft(deck, static_cast<int>(0.5 * kRate), 256);
  CHECK_FALSE(deck.isStretching());
  const auto rest = renderLeft(deck, static_cast<int>(1.5 * kRate), 256);
  scratch.insert(scratch.end(), rest.begin(), rest.end());
  CHECK(isFinite(scratch));
  CHECK(maxAbsolute(scratch) < 1.0F);
  CHECK(deck.isStretching());  // back through the stretcher after the scratch
  const double f = estimateFrequency(rest, static_cast<std::size_t>(0.9 * kRate), rest.size(), kRate);
  CHECK(std::abs(f - 600.0) / 600.0 < 0.01);
}

TEST_CASE("Keylock: stems are mixed first and stretched by the one stretcher", "[audio][keylock][stems]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(300.0, 12.0, 0.4F));
  DeckPlayer::StemBuffers stems;
  stems[0] = sineTrack(300.0, 12.0, 0.2F);
  stems[1] = sineTrack(300.0, 12.0, 0.2F);
  stems[2] = sineTrack(300.0, 12.0, 0.0F);
  stems[3] = sineTrack(300.0, 12.0, 0.0F);
  deck.loadStems(stems);
  deck.setPlaybackSpeed(1.07);
  deck.play();
  const auto out = renderLeft(deck, static_cast<int>(2.5 * kRate), 256);
  CHECK(deck.isStretching());
  CHECK(isFinite(out));
  const double f = estimateFrequency(out, static_cast<std::size_t>(0.6 * kRate), out.size(), kRate);
  CHECK(std::abs(f - 300.0) / 300.0 < 0.01);
  CHECK(maxAbsolute(out, static_cast<std::size_t>(0.6 * kRate)) > 0.3F);  // the stems actually sound

  // Muting a stem works through the stretcher too.
  deck.stemMixer().setMute(zyron::core::StemKind::Vocals, true);
  deck.stemMixer().setMute(zyron::core::StemKind::Drums, true);
  (void)renderLeft(deck, static_cast<int>(0.5 * kRate), 256);
  const auto muted = renderLeft(deck, static_cast<int>(0.5 * kRate), 256);
  CHECK(maxAbsolute(muted, 2000) < 0.01F);
}

TEST_CASE("Keylock: a 44.1 kHz track on a 48 kHz device keeps its pitch", "[audio][keylock]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(1000.0, 12.0, 0.5F, 44100.0));
  deck.setPlaybackSpeed(1.06);
  deck.play();
  const auto out = renderLeft(deck, static_cast<int>(2.5 * kRate), 256);
  const double f = estimateFrequency(out, static_cast<std::size_t>(0.6 * kRate), out.size(), kRate);
  CHECK(std::abs(f - 1000.0) / 1000.0 < 0.01);
  CHECK(deck.currentTimeSec() == Catch::Approx(2.5 * 1.06).margin(0.01));
}

TEST_CASE("Keylock: no heap activity on the audio thread, including primes, toggles and shifts", "[audio][keylock][rt]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(500.0, 20.0, 0.5F, 44100.0));
  DeckPlayer::StemBuffers stems;
  for (auto& s : stems) {
    s = sineTrack(500.0, 20.0, 0.1F, 44100.0);
  }
  deck.loadStems(stems);
  deck.setPlaybackSpeed(1.1);
  deck.play();
  (void)renderLeft(deck, 4800, 256);

  std::vector<float> left(512);
  std::vector<float> right(512);
  float* planes[2] = {left.data(), right.data()};
  {
    ScopedRealtimeGuard guard;
    for (int block = 0; block < 600; ++block) {
      if (block % 50 == 10) {
        deck.seekSeconds(1.0 + 0.5 * (block / 50));
      }
      if (block % 70 == 20) {
        deck.setKeylock(block % 140 != 20);
      }
      if (block % 40 == 5) {
        deck.setKeyShift(static_cast<float>((block / 40) % 5) - 2.0F);
      }
      if (block == 300) {
        deck.setLoopSeconds(4.0, 5.0, true);
      }
      if (block == 450) {
        deck.startScratch(0, 0.5, 0.4);
      }
      deck.render(planes, 2, 256);
    }
    CHECK(guard.allocationCount() == 0);
    CHECK(guard.deallocationCount() == 0);
  }
}

TEST_CASE("Keylock: CPU cost of one stretching deck", "[audio][keylock][bench]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(500.0, 40.0, 0.5F));
  deck.setPlaybackSpeed(1.06);
  deck.setKeyShift(1.0F);
  deck.play();
  std::vector<float> left(256);
  std::vector<float> right(256);
  float* planes[2] = {left.data(), right.data()};
  for (int i = 0; i < 200; ++i) {
    deck.render(planes, 2, 256);
  }
  const int blocks = 3000;  // 16 s of audio
  double worst = 0.0;
  const auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < blocks; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    deck.render(planes, 2, 256);
    worst = std::max(worst, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
  const double audioSeconds = blocks * 256.0 / kRate;
  const double load = seconds / audioSeconds;
  const double worstLoad = worst / (256.0 / kRate);
  std::printf("[bench] keylock+shift deck @48k/256: average %.2f %% of one core, worst block %.1f %% of its deadline\n",
              load * 100.0, worstLoad * 100.0);
  CHECK(isFinite(std::vector<float>(left)));
#ifdef NDEBUG
  CHECK(load < 0.15);        // generous: a regression to an unusable cost, not a benchmark gate
  CHECK(worstLoad < 0.75);
#endif
}

TEST_CASE("Keylock: cost of restarting the stretcher on a seek", "[.primebench]") {
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(sineTrack(500.0, 40.0, 0.5F));
  deck.setPlaybackSpeed(1.06);
  deck.play();
  std::vector<float> left(64);
  std::vector<float> right(64);
  float* planes[2] = {left.data(), right.data()};
  for (int i = 0; i < 200; ++i) {
    deck.render(planes, 2, 64);
  }
  double worst = 0.0;
  double total = 0.0;
  for (int i = 0; i < 50; ++i) {
    deck.seekSeconds(2.0 + i * 0.1);
    const auto t0 = std::chrono::steady_clock::now();
    deck.render(planes, 2, 64);
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    worst = std::max(worst, dt);
    total += dt;
    for (int k = 0; k < 20; ++k) deck.render(planes, 2, 64);
  }
  std::printf("[primebench] avg %.0f us, worst %.0f us per prime block\n", total / 50 * 1e6, worst * 1e6);
}

TEST_CASE("TimeStretcher: reports its latency and the pre-roll it needs", "[audio][keylock][bench]") {
  TimeStretcher stretcher;
  stretcher.prepare(kRate, 512);
  const int in = stretcher.inputLatency();
  const int out = stretcher.outputLatency();
  std::printf("[bench] stretcher @48k: input latency %d frames (%.1f ms), output latency %d frames (%.1f ms), "
              "prime length at 1.0x %d frames, at 1.06x %d, max %d\n",
              in, 1000.0 * in / kRate, out, 1000.0 * out / kRate, stretcher.primeLength(1.0), stretcher.primeLength(1.06),
              stretcher.maxPrimeLength());
  CHECK(in > 0);
  CHECK(out > 0);
  CHECK(stretcher.primeLength(1.0) == in + out);
  CHECK(stretcher.maxPrimeLength() >= stretcher.primeLength(TimeStretcher::kMaxRatio));
  CHECK(stretcher.primeLength(1.0) < static_cast<int>(0.4 * kRate));  // a seek never reads more than 0.4 s ahead
}

TEST_CASE("Keylock: a deck started later at the master's position stays on the beat with it", "[audio][keylock][sync]") {
  const auto clicks = generateClickTrack(kRate, 126.0, 30.0, 0.1);
  DeckPlayer master;
  master.prepare(kRate);
  master.loadTrack(makeTrack(clicks));
  master.setPlaybackSpeed(1.05);
  master.play();
  (void)renderLeft(master, static_cast<int>(2.0 * kRate), 256);

  DeckPlayer follower;
  follower.prepare(kRate);
  follower.loadTrack(makeTrack(clicks));
  follower.setPlaybackSpeed(1.05);
  follower.seek(master.currentFrame());  // what a phase-locked start does: begin at the master's audible position
  follower.play();

  const int total = static_cast<int>(3.0 * kRate);
  const auto a = renderLeft(master, total, 256);
  const auto b = renderLeft(follower, total, 256);
  const std::vector<float> aTail(a.begin() + 24000, a.end());
  const std::vector<float> bTail(b.begin() + 24000, b.end());
  const auto xc = crossCorrelation(aTail, bTail, 2400);
  std::printf("[bench] follower vs master: best lag %d frames (%.2f ms), correlation %.3f\n", xc.bestLag,
              1000.0 * xc.bestLag / kRate, static_cast<double>(xc.maxCorrelation));
  CHECK(std::abs(xc.bestLag) <= 48);  // within 1 ms
  CHECK(xc.maxCorrelation > 0.8F);
}

TEST_CASE("Keylock: works at 44.1 kHz and 96 kHz device rates too", "[audio][keylock]") {
  for (const double rate : {44100.0, 96000.0}) {
    DeckPlayer deck;
    deck.prepare(rate);
    deck.loadTrack(sineTrack(1000.0, 8.0, 0.5F, rate));
    deck.setPlaybackSpeed(1.06);
    deck.setKeyShift(2.0F);
    deck.play();
    const auto out = renderLeft(deck, static_cast<int>(2.5 * rate), 256);
    const double expected = 1000.0 * std::pow(2.0, 2.0 / 12.0);
    const double f = estimateFrequency(out, static_cast<std::size_t>(0.8 * rate), out.size(), rate);
    INFO("rate " << rate << " -> " << f << " Hz");
    CHECK(std::abs(f - expected) / expected < 0.01);
    CHECK(isFinite(out));
  }
}
