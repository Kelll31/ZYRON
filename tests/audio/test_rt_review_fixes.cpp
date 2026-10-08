// SPDX-License-Identifier: AGPL-3.0-only
// Regressions for the realtime review of the keylock / FX / master code: prime budget in every mode, no pitch blip on loop
// wraps, slow-tempo alignment, cue/master latency match, tail-after-fader flips. Offline renders only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include "Audio/DSP/ChannelStrip.hpp"
#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Engine/AudioGraph.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron;
using namespace zyron::audio;
using namespace zyron::test;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = std::numbers::pi;

std::shared_ptr<TrackBuffer> monoTrack(const std::vector<float>& mono) {
  auto track = std::make_shared<TrackBuffer>(2, static_cast<std::int64_t>(mono.size()), kRate);
  std::copy(mono.begin(), mono.end(), track->channelData(0));
  std::copy(mono.begin(), mono.end(), track->channelData(1));
  return track;
}

/// Frequency from upward zero crossings with linear interpolation in [from, to).
double zeroCrossingFrequency(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double first = -1.0;
  double last = -1.0;
  int crossings = 0;
  for (std::size_t i = std::max<std::size_t>(from, 1); i < to && i < x.size(); ++i) {
    if (x[i - 1] < 0.0F && x[i] >= 0.0F) {
      const double t = static_cast<double>(i - 1) + static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
      first = first < 0.0 ? t : first;
      last = t;
      ++crossings;
    }
  }
  return crossings < 4 ? 0.0 : static_cast<double>(crossings - 1) / ((last - first) / kRate);
}

std::vector<float> renderGraph(AudioGraph& graph, int frames, int block) {
  std::vector<float> left(static_cast<std::size_t>(frames));
  std::vector<float> right(static_cast<std::size_t>(frames));
  for (int done = 0; done < frames; done += block) {
    const int n = std::min(block, frames - done);
    float* planes[2] = {left.data() + done, right.data() + done};
    graph.render(planes, 2, n);
  }
  return left;
}

}  // namespace

TEST_CASE("Review: synced keylocked decks wrapping together keep their pitch across the wraps", "[audio][keylock][review]") {
  AudioGraph graph;
  graph.prepare(kRate);
  graph.mixer().setCrossfader(0.0F);
  // 4 kHz, a loop of exactly 2000 cycles: the source is seamless at the wrap, so any pitch change is the engine's.
  const auto tone = generateSine(kRate, 4000.0, 6.0, 0.2F);
  for (const auto id : {core::DeckId::A, core::DeckId::B}) {
    auto& deck = graph.deck(id);
    deck.loadTrack(monoTrack(tone));
    deck.setPlaybackSpeed(1.06);
    deck.setLoopSeconds(1.0, 1.5, true);
    deck.seekSeconds(1.2);
    deck.play();
  }
  graph.channel(core::DeckId::B).setVolume(0.0F);  // B only competes for the prime budget; A alone is heard
  const auto out = renderGraph(graph, static_cast<int>(4.0 * kRate), 128);
  REQUIRE(isFinite(out));
  REQUIRE(graph.deck(core::DeckId::A).isStretching());

  // Every window of 192 frames after the start-up: still 4 kHz (a varispeed fallback would read 4240 Hz).
  int measured = 0;
  int bad = 0;
  int lastBad = -1;
  int run = 0;
  int worstRun = 0;
  for (std::size_t at = static_cast<std::size_t>(0.5 * kRate); at + 192 < out.size(); at += 96) {
    const auto first = out.begin() + static_cast<std::ptrdiff_t>(at);
    if (maxAbsolute({first, first + 192}) < 0.05F) {
      continue;  // silent stretch
    }
    const double f = zeroCrossingFrequency(out, at, at + 192);
    ++measured;
    if (std::abs(f - 4000.0) / 4000.0 >= 0.02) {
      ++bad;
      run = bad == lastBad + 1 ? run + 1 : 1;
      worstRun = std::max(worstRun, run);
      lastBad = bad;
    } else {
      lastBad = -1;
    }
  }
  CHECK(measured > 100);
  // Each wrap costs a declicked jump (two windows at most); a fallback to the untouched signal would last far longer.
  CHECK(worstRun <= 3);
}

TEST_CASE("Review: four stem decks wrapping together restart at most one stretcher per block", "[audio][keylock][stems][review]") {
  AudioGraph graph;
  graph.prepare(kRate);
  graph.mixer().setCrossfader(0.0F);
  for (std::size_t d = 0; d < core::kDeckCount; ++d) {
    auto& deck = graph.deck(d);
    DeckPlayer::StemBuffers stems;
    for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
      stems[s] = monoTrack(generateSine(kRate, 300.0 + 100.0 * static_cast<double>(s), 6.0, 0.1F));
    }
    deck.loadTrack(stems[0]);
    deck.loadStems(stems);
    deck.setPlaybackSpeed(1.04);
    deck.setLoopSeconds(1.0, 1.5, true);
    deck.seekSeconds(1.2);
    deck.play();
  }
  std::vector<float> l(128);
  std::vector<float> r(128);
  float* planes[2] = {l.data(), r.data()};
  float peak = 0.0F;
  for (int done = 0; done < static_cast<int>(3.0 * kRate); done += 128) {
    graph.render(planes, 2, 128);
    CHECK(graph.lastBlockPrimes() <= 1);
    for (const float v : l) {
      peak = std::max(peak, std::abs(v));
    }
  }
  CHECK(graph.maxBlockPrimes() == 1);
  CHECK(peak > 0.05F);
  for (std::size_t d = 0; d < core::kDeckCount; ++d) {
    CHECK(graph.deck(d).isStretching());
  }
}

TEST_CASE("Review: keylock at 0.15x stays aligned to the playhead", "[audio][keylock][review]") {
  // A silent track with one 1 kHz burst at 1.0 s: at 0.15x it is heard at 1.0 / 0.15 = 6.67 s.
  std::vector<float> mono(static_cast<std::size_t>(3.0 * kRate), 0.0F);
  const auto burstAt = static_cast<std::size_t>(1.0 * kRate);
  for (std::size_t i = 0; i < 2400; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / 2400.0);
    mono[burstAt + i] = static_cast<float>(0.5 * w * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / kRate));
  }
  DeckPlayer deck;
  deck.prepare(kRate);
  deck.loadTrack(monoTrack(mono));
  deck.setPlaybackSpeed(0.15);
  deck.play();
  const int frames = static_cast<int>(10.0 * kRate);
  std::vector<float> out(static_cast<std::size_t>(frames));
  std::vector<float> right(256);
  for (int done = 0; done < frames; done += 256) {
    float* planes[2] = {out.data() + done, right.data()};
    deck.render(planes, 2, std::min(256, frames - done));
  }
  double weighted = 0.0;
  double total = 0.0;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const double e = static_cast<double>(out[i]) * static_cast<double>(out[i]);
    weighted += e * static_cast<double>(i);
    total += e;
  }
  REQUIRE(total > 0.0);
  const double centroid = weighted / total / kRate;
  CHECK(centroid == Catch::Approx((1.0 + 0.025) / 0.15).margin(0.15));
}

TEST_CASE("Review: the cue bus is delayed by the limiter lookahead, limiter on or off", "[audio][cue][limiter][review]") {
  for (const bool limiterOn : {true, false}) {
    AudioGraph graph;
    graph.prepare(kRate);
    graph.mixer().setCrossfader(0.0F);
    graph.mixer().setMasterProcessing(false, limiterOn);
    graph.mixer().setCue(0, true);
    graph.cueRouter().setMode(HeadphoneRoutingMode::MultiChannel);
    graph.cueRouter().setHeadphoneMix(0.0F);
    graph.cueRouter().setHeadphoneVolume(1.0F);
    graph.deck(core::DeckId::A).loadTrack(monoTrack(generateNoise(static_cast<int>(2.0 * kRate), 0.3F, 3)));
    graph.deck(core::DeckId::A).play();
    graph.cueRouter().reset();
    REQUIRE(graph.mixer().masterLimiter().latencySamples() > 0);

    const int frames = static_cast<int>(1.0 * kRate);
    std::vector<std::vector<float>> ch(4, std::vector<float>(static_cast<std::size_t>(frames)));
    for (int done = 0; done < frames; done += 256) {
      float* planes[4] = {ch[0].data() + done, ch[1].data() + done, ch[2].data() + done, ch[3].data() + done};
      graph.render(planes, 4, std::min(256, frames - done));
    }
    int bestLag = 0;
    double best = -1.0;
    for (int lag = -100; lag <= 100; ++lag) {
      double sum = 0.0;
      for (int i = 20000; i < 40000; ++i) {
        sum += static_cast<double>(ch[0][static_cast<std::size_t>(i)]) *
               static_cast<double>(ch[2][static_cast<std::size_t>(i + lag)]);
      }
      if (sum > best) {
        best = sum;
        bestLag = lag;
      }
    }
    INFO("limiter " << limiterOn);
    CHECK(bestLag == 0);
    CHECK(best > 0.0);
  }
}

TEST_CASE("Review: flipping tailAfterFader while a send tail rings does not step the tail", "[audio][fx][review]") {
  const auto burst = generateSine(kRate, 440.0, 0.4, 0.5F);
  const auto run = [&](bool flip, bool startPost = false) {
    auto strip = std::make_unique<ChannelStrip>();
    strip->prepare(kRate);
    strip->setFxBeatSeconds(0.25F);
    strip->setVolume(0.3F);
    strip->setFx(0, core::FxType::Echo, true, 0.8F, 0.8F, startPost);
    std::vector<float> out;
    std::vector<float> l(256);
    std::vector<float> r(256);
    for (int block = 0; block < 375; ++block) {  // 2 s
      if (block == 150 && flip) {
        strip->setFx(0, core::FxType::Echo, true, 0.8F, 0.8F, true);  // 0.8 s in: an echo is ringing
      }
      for (int i = 0; i < 256; ++i) {
        const std::size_t at = static_cast<std::size_t>(block) * 256 + static_cast<std::size_t>(i);
        l[static_cast<std::size_t>(i)] = at < burst.size() ? burst[at] : 0.0F;
        r[static_cast<std::size_t>(i)] = l[static_cast<std::size_t>(i)];
      }
      float* planes[2] = {l.data(), r.data()};
      strip->process(planes, 2, 256);
      out.insert(out.end(), l.begin(), l.end());
    }
    return std::make_pair(std::move(out), strip->fxUnit(0).postFader());
  };
  const auto [flipped, postAfter] = run(true);
  const auto [steady, postSteady] = run(false);
  CHECK(isFinite(flipped));
  CHECK(postAfter);  // the flip did happen
  CHECK_FALSE(postSteady);
  // The tail is a 440 Hz tone with a slowly changing level: no step may exceed the slope of a sine at the local peak
  // (a one-step rescale by the fader's 0.3 would be a step of up to 70 % of the peak, ten times that).
  const auto worstStepOverSlope = [](const std::vector<float>& x) {
    constexpr float kSlope = 2.0F * 3.14159265F * 440.0F / 48000.0F;
    constexpr std::size_t kHalfWindow = 150;
    float worst = 0.0F;
    for (std::size_t i = 38400 + kHalfWindow; i + kHalfWindow < x.size(); ++i) {
      float peak = 0.0F;
      for (std::size_t k = i - kHalfWindow; k <= i + kHalfWindow; ++k) {
        peak = std::max(peak, std::abs(x[k]));
      }
      worst = std::max(worst, std::abs(x[i] - x[i - 1]) / (kSlope * peak + 1.0e-4F));
    }
    return worst;
  };
  INFO("flipped " << worstStepOverSlope(flipped) << " steady " << worstStepOverSlope(steady));
  CHECK(worstStepOverSlope(steady) < 1.3F);
  CHECK(worstStepOverSlope(flipped) < 1.3F);
}
