// SPDX-License-Identifier: AGPL-3.0-only
// The DJ-sound features of the engine: channel FX with echo-out tails, automatic track gain trim, the master glue
// compressor, the lookahead limiter, and the commands that drive them end to end.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <vector>

#include "Audio/Bridge/CommandBridge.hpp"
#include "Audio/DSP/ChannelStrip.hpp"
#include "Audio/DSP/Mixer.hpp"
#include "Audio/Engine/AudioGraph.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron;
using namespace zyron::audio;
using namespace zyron::test;

namespace {

constexpr double kRate = 48000.0;

std::shared_ptr<TrackBuffer> sineTrack(double freq, double seconds, float amp = 0.4F) {
  const auto mono = generateSine(kRate, freq, seconds, amp);
  auto track = std::make_shared<TrackBuffer>(2, static_cast<std::int64_t>(mono.size()), kRate);
  std::copy(mono.begin(), mono.end(), track->channelData(0));
  std::copy(mono.begin(), mono.end(), track->channelData(1));
  return track;
}

/// Runs `input` (mono, copied to both channels) through the strip in blocks and returns the left output.
std::vector<float> runStrip(ChannelStrip& strip, const std::vector<float>& input, int blockSize = 256) {
  std::vector<float> out(input.size());
  std::vector<float> left(static_cast<std::size_t>(blockSize));
  std::vector<float> right(static_cast<std::size_t>(blockSize));
  for (std::size_t done = 0; done < input.size(); done += static_cast<std::size_t>(blockSize)) {
    const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(blockSize), input.size() - done));
    std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(done), n, left.begin());
    std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(done), n, right.begin());
    float* planes[2] = {left.data(), right.data()};
    strip.process(planes, 2, n);
    std::copy_n(left.begin(), n, out.begin() + static_cast<std::ptrdiff_t>(done));
  }
  return out;
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double sum = 0.0;
  for (std::size_t i = from; i < to && i < x.size(); ++i) {
    sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
  }
  return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
}

float maxStep(const std::vector<float>& x, std::size_t from = 1) {
  float worst = 0.0F;
  for (std::size_t i = std::max<std::size_t>(from, 1); i < x.size(); ++i) {
    worst = std::max(worst, std::abs(x[i] - x[i - 1]));
  }
  return worst;
}

/// A fresh strip that is transparent apart from what the test switches on.
std::unique_ptr<ChannelStrip> makeStrip() {
  auto strip = std::make_unique<ChannelStrip>();
  strip->prepare(kRate);
  return strip;
}

/// Pushes `input` (mono) through a stereo Mixer channel 0 and returns the left master output.
std::vector<float> runMixer(Mixer& mixer, const std::vector<float>& input, int blockSize = 256,
                            std::vector<float>* glueReductionDb = nullptr) {
  std::vector<float> out(input.size());
  std::vector<float> silence(static_cast<std::size_t>(blockSize), 0.0F);
  std::vector<float> outR(static_cast<std::size_t>(blockSize));
  std::vector<float> outL(static_cast<std::size_t>(blockSize));
  for (std::size_t done = 0; done < input.size(); done += static_cast<std::size_t>(blockSize)) {
    const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(blockSize), input.size() - done));
    const float* inL[2] = {input.data() + done, silence.data()};
    const float* inR[2] = {input.data() + done, silence.data()};
    mixer.process(inL, inR, 2, outL.data(), outR.data(), n);
    std::copy_n(outL.begin(), n, out.begin() + static_cast<std::ptrdiff_t>(done));
    if (glueReductionDb != nullptr) {
      glueReductionDb->push_back(mixer.glueCompressor().gainReductionDb());
    }
  }
  return out;
}

void configureMixer(Mixer& mixer) {
  mixer.prepare(kRate);
  mixer.setChannelAssign(0, CrossfaderAssign::Thru);
  mixer.setChannelAssign(1, CrossfaderAssign::Thru);
}

double soakSeconds() {
  double seconds = 120.0;
#if defined(_MSC_VER)
  char* value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, "ZYRON_SOAK_SECONDS") == 0 && value != nullptr) {
    seconds = std::atof(value);
    std::free(value);
  }
#else
  if (const char* value = std::getenv("ZYRON_SOAK_SECONDS")) {
    seconds = std::atof(value);
  }
#endif
  return seconds;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------- bridge

TEST_CASE("Bridge: the DJ-sound commands map to RT messages", "[audio][bridge][dj-sound]") {
  using namespace zyron::core;
  {
    const auto m = CommandBridge::translateCommand(SetKeylock{DeckId::C, false});
    REQUIRE(m.has_value());
    CHECK(m->type == RtMessageType::DeckKeylock);
    CHECK(m->deck == DeckId::C);
    CHECK_FALSE(m->data.keylockEnabled);
  }
  {
    const auto m = CommandBridge::translateCommand(SetKeyShift{DeckId::B, -2.5F});
    REQUIRE(m.has_value());
    CHECK(m->type == RtMessageType::DeckKeyShift);
    CHECK(m->data.keyShiftSemitones == -2.5F);
  }
  {
    const auto m = CommandBridge::translateCommand(SetFx{DeckId::D, 1, FxType::Phaser, true, 0.25F, 0.75F, false});
    REQUIRE(m.has_value());
    CHECK(m->type == RtMessageType::DeckFx);
    CHECK(m->deck == DeckId::D);
    CHECK(m->data.fx.slot == 1);
    CHECK(m->data.fx.type == FxType::Phaser);
    CHECK(m->data.fx.enabled);
    CHECK(m->data.fx.wet == 0.25F);
    CHECK(m->data.fx.param == 0.75F);
    CHECK_FALSE(m->data.fx.tailAfterFader);
  }
  {
    const auto m = CommandBridge::translateCommand(SetFxTempo{DeckId::A, 0.345});
    REQUIRE(m.has_value());
    CHECK(m->type == RtMessageType::DeckFxTempo);
    CHECK(m->data.fxBeatSeconds == Catch::Approx(0.345F));
  }
  {
    const auto m = CommandBridge::translateCommand(SetTrackGainTrim{DeckId::A, -3.5F});
    REQUIRE(m.has_value());
    CHECK(m->type == RtMessageType::DeckTrackTrim);
    CHECK(m->data.trackTrimDb == -3.5F);
  }
  {
    const auto m = CommandBridge::translateCommand(SetMasterProcessing{true, false});
    REQUIRE(m.has_value());
    CHECK(m->type == RtMessageType::MixerProcessing);
    CHECK(m->data.processing.glue);
    CHECK_FALSE(m->data.processing.limiter);
  }
}

TEST_CASE("AudioGraph applies the DJ-sound commands on the audio thread", "[audio][graph][dj-sound]") {
  using namespace zyron::core;
  AudioGraph graph;
  graph.prepare(kRate);
  CHECK(graph.deck(DeckId::A).keylockEnabled());  // default on
  CHECK(graph.mixer().masterLimiter().isEnabled());
  CHECK(graph.mixer().masterLimiter().ceilingDb() == Catch::Approx(-0.3F));
  CHECK(graph.mixer().glueCompressor().isEnabled());

  REQUIRE(graph.bridge().pushCommand(SetKeylock{DeckId::B, false}));
  REQUIRE(graph.bridge().pushCommand(SetKeyShift{DeckId::B, 3.0F}));
  REQUIRE(graph.bridge().pushCommand(SetFx{DeckId::B, 1, FxType::Echo, true, 0.6F, 0.4F, true}));
  REQUIRE(graph.bridge().pushCommand(SetTrackGainTrim{DeckId::B, -5.0F}));
  REQUIRE(graph.bridge().pushCommand(SetMasterProcessing{false, false}));

  std::vector<float> l(256);
  std::vector<float> r(256);
  float* out[2] = {l.data(), r.data()};
  graph.render(out, 2, 256);

  CHECK_FALSE(graph.deck(DeckId::B).keylockEnabled());
  CHECK(graph.deck(DeckId::A).keylockEnabled());
  CHECK(graph.deck(DeckId::B).keyShift() == 3.0F);
  CHECK(graph.channel(DeckId::B).fxUnit(1).type() == FxType::Echo);
  CHECK(graph.channel(DeckId::B).fxUnit(1).enabled());
  CHECK(graph.channel(DeckId::B).fxUnit(1).postFader());
  CHECK(graph.channel(DeckId::A).fxUnit(1).type() == FxType::None);
  CHECK(graph.channel(DeckId::B).trackGainTrimDb() == -5.0F);
  CHECK_FALSE(graph.mixer().glueCompressor().isEnabled());
  CHECK_FALSE(graph.mixer().masterLimiter().isEnabled());
}

// ---------------------------------------------------------------------------------------------------- FX

TEST_CASE("FX: wet 0 is a bit-exact bypass for every effect type", "[audio][fx][dj-sound]") {
  const auto noise = generateNoise(48000, 0.3F, 7);
  for (const auto type : {core::FxType::Echo, core::FxType::Reverb, core::FxType::Flanger, core::FxType::Phaser,
                          core::FxType::Delay}) {
    auto strip = makeStrip();
    strip->setFx(0, type, true, 0.0F, 0.5F, true);
    const auto out = runStrip(*strip, noise);
    INFO("effect " << static_cast<int>(type));
    CHECK(isFinite(out));
    // The strip has trim 1, flat EQ and no filter; compare against an untouched strip.
    auto plain = makeStrip();
    const auto reference = runStrip(*plain, noise);
    CHECK(nullTestMaxDiff(out, reference) < 1e-6F);
  }
}

TEST_CASE("FX: every effect produces a finite, bounded signal and a different one from the dry", "[audio][fx][dj-sound]") {
  const auto noise = generateNoise(96000, 0.3F, 11);
  auto plain = makeStrip();
  const auto reference = runStrip(*plain, noise);
  for (const auto type : {core::FxType::Echo, core::FxType::Reverb, core::FxType::Flanger, core::FxType::Phaser,
                          core::FxType::Delay}) {
    auto strip = makeStrip();
    strip->setFxBeatSeconds(0.345F);
    strip->setFx(0, type, true, 0.7F, 0.6F, true);
    const auto out = runStrip(*strip, noise);
    INFO("effect " << static_cast<int>(type));
    CHECK(isFinite(out));
    CHECK(maxAbsolute(out) < 2.0F);
    CHECK(nullTestMaxDiff(out, reference) > 0.01F);
  }
}

TEST_CASE("FX: the echo repeats one beat after the sound", "[audio][fx][dj-sound]") {
  auto strip = makeStrip();
  strip->setFxBeatSeconds(0.25F);
  strip->setFx(0, core::FxType::Echo, true, 1.0F, 0.2F, true);
  (void)runStrip(*strip, std::vector<float>(static_cast<std::size_t>(0.6 * kRate), 0.0F));  // let the time settle

  std::vector<float> input(static_cast<std::size_t>(1.0 * kRate), 0.0F);
  constexpr std::size_t kImpulse = 2000;
  for (std::size_t i = 0; i < 40; ++i) {
    input[kImpulse + i] = 0.8F * std::sin(static_cast<float>(i) * 0.5F) * (1.0F - static_cast<float>(i) / 40.0F);
  }
  const auto out = runStrip(*strip, input);

  // The dry burst passes untouched (a send, not a replacement) ...
  CHECK(maxAbsolute({out.begin() + kImpulse, out.begin() + kImpulse + 60}) > 0.4F);
  // ... and the first echo peaks one beat (12000 frames) later.
  std::size_t peakAt = 0;
  float peak = 0.0F;
  for (std::size_t i = kImpulse + 6000; i < out.size(); ++i) {
    if (std::abs(out[i]) > peak) {
      peak = std::abs(out[i]);
      peakAt = i;
    }
  }
  CHECK(peak > 0.05F);
  CHECK(static_cast<double>(peakAt) == Catch::Approx(static_cast<double>(kImpulse) + 12000.0).margin(60.0));
}

TEST_CASE("FX: closing the fader leaves an echo ringing out when the effect sits after it", "[audio][fx][dj-sound]") {
  const auto tone = generateSine(kRate, 440.0, 3.0, 0.5F);
  const std::size_t closeAt = static_cast<std::size_t>(1.0 * kRate);

  const auto render = [&](bool tailAfterFader) {
    auto strip = makeStrip();
    strip->setFxBeatSeconds(0.25F);
    strip->setFx(0, core::FxType::Echo, true, 0.8F, 0.8F, tailAfterFader);
    auto first = runStrip(*strip, {tone.begin(), tone.begin() + static_cast<std::ptrdiff_t>(closeAt)});
    strip->setVolume(0.0F);  // the DJ pulls the fader down while the track keeps playing
    const auto second = runStrip(*strip, {tone.begin() + static_cast<std::ptrdiff_t>(closeAt), tone.end()});
    first.insert(first.end(), second.begin(), second.end());
    return first;
  };

  const auto post = render(true);
  const auto pre = render(false);
  CHECK(isFinite(post));
  const std::size_t from = closeAt + static_cast<std::size_t>(0.15 * kRate);  // the fader's own 10 ms smoothing is over
  const std::size_t to = closeAt + static_cast<std::size_t>(0.9 * kRate);
  const double tail = rms(post, from, to);
  INFO("tail after the fader: " << tail << ", pre-fader effect: " << rms(pre, from, to));
  CHECK(tail > 0.02);                 // the echoes are clearly audible (-34 dBFS and up) ...
  CHECK(rms(pre, from, to) < 1e-3);   // ... whereas a pre-fader effect is cut with the fader
  // and the tail decays rather than lasting forever
  CHECK(rms(post, closeAt + static_cast<std::size_t>(2.0 * kRate), closeAt + static_cast<std::size_t>(2.0 * kRate) + 12000) <
        tail);
}

TEST_CASE("FX: a reverb tail rings on after the fader closes", "[audio][fx][dj-sound]") {
  const auto noise = generateNoise(static_cast<int>(0.5 * kRate), 0.3F, 5);
  auto strip = makeStrip();
  strip->setFx(1, core::FxType::Reverb, true, 0.8F, 0.9F, true);
  (void)runStrip(*strip, noise);
  strip->setVolume(0.0F);
  const auto tail = runStrip(*strip, noise);
  CHECK(rms(tail, 4800, 24000) > 0.01);
}

TEST_CASE("FX: changing and disabling effects never clicks", "[audio][fx][dj-sound]") {
  auto strip = makeStrip();
  strip->setFxBeatSeconds(0.3F);
  const auto tone = generateSine(kRate, 330.0, 6.0, 0.3F);
  std::vector<float> all;
  const auto feed = [&](std::size_t from, double seconds) {
    const auto n = static_cast<std::size_t>(seconds * kRate);
    const auto part = runStrip(*strip, {tone.begin() + static_cast<std::ptrdiff_t>(from),
                                        tone.begin() + static_cast<std::ptrdiff_t>(from + n)});
    all.insert(all.end(), part.begin(), part.end());
    return from + n;
  };
  std::size_t at = 0;
  strip->setFx(0, core::FxType::Echo, true, 0.5F, 0.5F, true);
  at = feed(at, 0.8);
  strip->setFx(0, core::FxType::Reverb, true, 0.5F, 0.5F, true);  // type switch: echo fades, reverb starts
  at = feed(at, 0.8);
  strip->setFx(0, core::FxType::Flanger, true, 0.5F, 0.3F, true);
  at = feed(at, 0.8);
  strip->setFx(0, core::FxType::Flanger, false, 0.5F, 0.3F, true);  // disable
  at = feed(at, 0.8);
  strip->setFx(0, core::FxType::Phaser, true, 0.6F, 0.5F, true);
  at = feed(at, 0.8);
  strip->setFx(0, core::FxType::None, true, 0.6F, 0.5F, true);  // empty the slot
  at = feed(at, 0.8);
  CHECK(isFinite(all));
  // A 330 Hz sine at 0.3 never steps more than 0.013 per sample; effects add some content, a click is far larger.
  CHECK(maxStep(all) < 0.12F);
  CHECK_FALSE(strip->fxUnit(0).isActive());  // everything faded out and went idle
}

TEST_CASE("FX: garbage settings that bypass validation cannot break the channel", "[audio][fx][dj-sound]") {
  auto strip = makeStrip();
  const auto noise = generateNoise(24000, 0.3F, 17);
  strip->setFx(0, static_cast<core::FxType>(99), true, 0.5F, 0.5F, true);  // unknown type: treated as an empty slot
  strip->setFx(1, core::FxType::Echo, true, std::numeric_limits<float>::quiet_NaN(),
               std::numeric_limits<float>::infinity(), true);
  strip->setFx(7, core::FxType::Reverb, true, 0.5F, 0.5F, true);   // no such slot: ignored
  strip->setFx(-1, core::FxType::Reverb, true, 0.5F, 0.5F, true);
  strip->setFxBeatSeconds(std::numeric_limits<float>::quiet_NaN());
  CHECK(strip->fxUnit(0).type() == core::FxType::None);
  CHECK(isFinite(runStrip(*strip, noise)));
}

// ---------------------------------------------------------------------------------------------------- trim

TEST_CASE("Track gain trim scales the channel before the gain, smoothly", "[audio][trim][dj-sound]") {
  auto strip = makeStrip();
  const auto tone = generateSine(kRate, 1000.0, 3.0, 0.2F);
  const auto first = runStrip(*strip, {tone.begin(), tone.begin() + 48000});
  strip->setTrackGainTrimDb(6.0F);
  const auto second = runStrip(*strip, {tone.begin() + 48000, tone.begin() + 96000});
  strip->setTrackGainTrimDb(-6.0F);
  strip->setGainDb(6.0F);  // the user's gain knob is independent: -6 + 6 = unity
  const auto third = runStrip(*strip, {tone.begin() + 96000, tone.end()});

  CHECK(rms(first, 4800, 48000) == Catch::Approx(0.2 / std::sqrt(2.0)).epsilon(0.02));
  CHECK(rms(second, 9600, 48000) / rms(first, 4800, 48000) == Catch::Approx(1.995).epsilon(0.02));
  CHECK(rms(third, 9600, 48000) / rms(first, 4800, 48000) == Catch::Approx(1.0).epsilon(0.02));
  CHECK(strip->trackGainTrimDb() == -6.0F);

  std::vector<float> joined = first;
  joined.insert(joined.end(), second.begin(), second.end());
  CHECK(maxStep(joined) < 0.2F * 2.0F * 3.1416F * 1000.0F / 48000.0F * 2.0F + 0.01F);  // no step at the change

  strip->setTrackGainTrimDb(50.0F);
  CHECK(strip->trackGainTrimDb() == 12.0F);  // clamped
  strip->setTrackGainTrimDb(std::numeric_limits<float>::quiet_NaN());
  CHECK(strip->trackGainTrimDb() == 12.0F);  // ignored
}

// ---------------------------------------------------------------------------------------------------- master

TEST_CASE("Master: the limiter is on by default with a -0.3 dBFS ceiling", "[audio][master][dj-sound]") {
  Mixer mixer;
  mixer.prepare(kRate);
  CHECK(mixer.masterLimiter().isEnabled());
  CHECK(mixer.masterLimiter().ceilingDb() == Catch::Approx(-0.3F));
  CHECK(mixer.glueCompressor().isEnabled());
}

TEST_CASE("Master: the output never exceeds the ceiling, whatever comes in", "[audio][master][limiter][dj-sound]") {
  const float ceiling = std::pow(10.0F, -0.3F / 20.0F);
  Mixer mixer;
  configureMixer(mixer);
  mixer.setMasterProcessing(false, true);  // limiter alone

  auto stress = generateNoise(96000, 4.0F, 3);
  const auto burst = generateSine(kRate, 80.0, 1.0, 6.0F);
  stress.insert(stress.end(), burst.begin(), burst.end());
  stress[40000] = 9.0F;  // an isolated full-scale spike
  stress[40001] = -9.0F;
  const auto out = runMixer(mixer, stress, 128);
  CHECK(isFinite(out));
  CHECK(maxAbsolute(out) <= ceiling + 1e-6F);

  // The same through the whole chain with the glue in front.
  Mixer mixer2;
  configureMixer(mixer2);
  const auto out2 = runMixer(mixer2, stress, 1000);
  CHECK(maxAbsolute(out2) <= ceiling + 1e-6F);
}

TEST_CASE("Master: the limiter squeezes a hot sine instead of clipping it", "[audio][master][limiter][dj-sound]") {
  Mixer mixer;
  configureMixer(mixer);
  mixer.setMasterProcessing(false, true);
  const auto hot = generateSine(kRate, 1000.0, 1.5, 1.5F);  // +3.5 dBFS
  const auto out = runMixer(mixer, hot);
  const std::vector<float> steady(out.begin() + 24000, out.begin() + 72000);  // a whole number of cycles
  const float fundamental = measureMagnitudeAt(steady, kRate, 1000.0);
  const float third = measureMagnitudeAt(steady, kRate, 3000.0);
  INFO("fundamental " << fundamental << ", 3rd harmonic " << third);
  CHECK(fundamental == Catch::Approx(0.9659F).margin(0.03F));
  CHECK(third / fundamental < 0.01F);  // a hard clip at this depth would put the 3rd harmonic around 0.1
}

TEST_CASE("Master: the limiter can be switched off", "[audio][master][limiter][dj-sound]") {
  Mixer mixer;
  configureMixer(mixer);
  mixer.setMasterProcessing(false, false);
  const auto hot = generateSine(kRate, 1000.0, 0.5, 1.5F);
  const auto out = runMixer(mixer, hot);
  CHECK(maxAbsolute(out) > 1.4F);
}

TEST_CASE("Master: NaN and infinity from upstream never reach the output", "[audio][master][dj-sound]") {
  Mixer mixer;
  configureMixer(mixer);
  std::vector<float> input = generateSine(kRate, 500.0, 0.5, 0.3F);
  input[1000] = std::numeric_limits<float>::quiet_NaN();
  input[2000] = std::numeric_limits<float>::infinity();
  input[3000] = -std::numeric_limits<float>::infinity();
  const auto out = runMixer(mixer, input);
  CHECK(isFinite(out));
  CHECK(maxAbsolute(out) <= 1.0F);
}

TEST_CASE("Master: one track at its nominal level passes the glue untouched", "[audio][master][glue][dj-sound]") {
  Mixer mixer;
  configureMixer(mixer);
  // A well-mastered track at its nominal level: -15 dBFS RMS, -12 dBFS peak.
  const auto tone = generateSine(kRate, 220.0, 3.0, 0.25F);
  std::vector<float> reduction;
  const auto out = runMixer(mixer, tone, 256, &reduction);
  CHECK(*std::max_element(reduction.begin(), reduction.end()) < 0.05F);
  // 48 samples of limiter lookahead; apart from that the signal is the input.
  const std::vector<float> a(out.begin() + 4800, out.end());
  const std::vector<float> b(tone.begin() + 4800 - 48, tone.end() - 48);
  CHECK(nullTestMaxDiff(a, b) < 1e-4F);
}

TEST_CASE("Master: a hot signal gets gentle, steady gain reduction", "[audio][master][glue][dj-sound]") {
  Mixer mixer;
  configureMixer(mixer);
  const auto hot = generateSine(kRate, 220.0, 4.0, 0.9F);  // -3.9 dBFS RMS
  std::vector<float> reduction;
  const auto out = runMixer(mixer, hot, 256, &reduction);
  const float settled = reduction.back();
  INFO("settled gain reduction " << settled << " dB");
  CHECK(settled > 1.5F);
  CHECK(settled < 4.0F);
  CHECK(maxAbsolute(out) < 0.9F);
  // No modulation once settled: a steady tone must not pump.
  const auto [lo, hi] = std::minmax_element(reduction.begin() + 400, reduction.end());
  CHECK(*hi - *lo < 0.05F);
}

TEST_CASE("Master: a kick-driven loop at 0 dB trim does not pump", "[audio][master][glue][dj-sound]") {
  // A four-on-the-floor kick (55 Hz, 120 ms decay, peak -2 dBFS) over a steady bed at -24 dBFS RMS.
  const int total = static_cast<int>(10.0 * kRate);
  std::vector<float> mix(static_cast<std::size_t>(total));
  const auto bed = generateNoise(total, 0.1F, 21);
  for (int i = 0; i < total; ++i) {
    const double t = std::fmod(static_cast<double>(i) / kRate, 0.5);
    const double kick = 0.8 * std::exp(-t / 0.12) * std::sin(2.0 * std::numbers::pi * 55.0 * t);
    mix[static_cast<std::size_t>(i)] = static_cast<float>(kick) + bed[static_cast<std::size_t>(i)];
  }
  Mixer mixer;
  configureMixer(mixer);
  std::vector<float> reduction;
  const auto out = runMixer(mixer, mix, 128, &reduction);
  CHECK(isFinite(out));
  const auto [lo, hi] = std::minmax_element(reduction.begin() + static_cast<std::ptrdiff_t>(reduction.size() / 4),
                                            reduction.end());
  INFO("gain reduction range " << *lo << " .. " << *hi << " dB");
  CHECK(*hi < 3.0F);          // gentle: never more than a few dB
  CHECK(*hi - *lo < 2.0F);    // and the movement between kicks is small: that is what pumping would be
}

TEST_CASE("Master: switching the glue off releases it smoothly", "[audio][master][glue][dj-sound]") {
  Mixer mixer;
  configureMixer(mixer);
  mixer.setMasterProcessing(true, false);
  const auto hot = generateSine(kRate, 220.0, 2.0, 0.9F);
  std::vector<float> all = runMixer(mixer, hot);
  CHECK(mixer.glueCompressor().gainReductionDb() > 1.0F);
  mixer.setMasterProcessing(false, false);
  const auto more = runMixer(mixer, hot);
  all.insert(all.end(), more.begin(), more.end());
  CHECK(mixer.glueCompressor().gainReductionDb() < 0.1F);
  CHECK(maxStep(all) < 0.9F * 2.0F * 3.1416F * 220.0F / 48000.0F * 1.1F);  // no step when it lets go
  CHECK(maxAbsolute(more) > 0.89F);
}

// ---------------------------------------------------------------------------------------------------- whole graph

TEST_CASE("AudioGraph: 4 decks with keylock, key shift, FX, trim and master processing allocate nothing",
          "[audio][graph][rt][dj-sound]") {
  using namespace zyron::core;
  AudioGraph graph;
  graph.prepare(kRate);
  for (std::size_t i = 0; i < kDeckCount; ++i) {
    graph.deck(i).loadTrack(sineTrack(220.0 * static_cast<double>(i + 1), 30.0));
    graph.deck(i).setPlaybackSpeed(1.0 + 0.02 * static_cast<double>(i + 1));
    graph.deck(i).play();
  }
  DeckPlayer::StemBuffers stems;
  for (auto& s : stems) {
    s = sineTrack(330.0, 30.0, 0.1F);
  }
  graph.deck(0).loadStems(stems);
  graph.deck(0).play();

  std::vector<float> l(256);
  std::vector<float> r(256);
  float* out[2] = {l.data(), r.data()};
  for (int i = 0; i < 40; ++i) {
    graph.render(out, 2, 256);
  }

  const FxType types[] = {FxType::Echo, FxType::Reverb, FxType::Flanger, FxType::Phaser, FxType::Delay, FxType::None};
  {
    ScopedRealtimeGuard guard;
    for (int block = 0; block < 800; ++block) {
      const auto deck = static_cast<DeckId>(block % 4);
      if (block % 7 == 0) {
        (void)graph.bridge().pushCommand(SetFx{deck, block % 2, types[(block / 7) % 6], block % 3 != 0, 0.5F, 0.5F,
                                               block % 5 != 0});
      }
      if (block % 11 == 0) {
        (void)graph.bridge().pushCommand(SetFxTempo{deck, 0.3 + 0.01 * (block % 20)});
      }
      if (block % 13 == 0) {
        (void)graph.bridge().pushCommand(SetKeyShift{deck, static_cast<float>((block / 13) % 7) - 3.0F});
      }
      if (block % 17 == 0) {
        (void)graph.bridge().pushCommand(SetKeylock{deck, (block / 17) % 2 == 0});
      }
      if (block % 19 == 0) {
        (void)graph.bridge().pushCommand(SetTrackGainTrim{deck, static_cast<float>((block / 19) % 9) - 4.0F});
      }
      if (block % 101 == 0) {
        (void)graph.bridge().pushCommand(SetMasterProcessing{(block / 101) % 2 == 0, (block / 101) % 3 != 0});
      }
      if (block % 61 == 0) {
        graph.deck(deck).seekSeconds(1.0 + static_cast<double>(block % 9));
      }
      graph.render(out, 2, 256);
    }
    CHECK(guard.allocationCount() == 0);
    CHECK(guard.deallocationCount() == 0);
  }
  CHECK(isFinite(l));
  CHECK(isFinite(r));
}

TEST_CASE("AudioGraph: CPU of 4 stretching decks with FX and master processing", "[audio][graph][cpu][bench][dj-sound]") {
  using namespace zyron::core;
  for (const int blockSize : {64, 128, 256}) {
    AudioGraph graph;
    graph.prepare(kRate);
    for (std::size_t i = 0; i < kDeckCount; ++i) {
      graph.deck(i).loadTrack(sineTrack(220.0 * static_cast<double>(i + 1), 30.0));
      graph.deck(i).setPlaybackSpeed(0.95 + 0.04 * static_cast<double>(i));
      graph.deck(i).setKeyShift(static_cast<float>(i) - 1.0F);
      graph.deck(i).play();
      graph.channel(i).setFx(0, FxType::Echo, true, 0.4F, 0.5F, true);
      graph.channel(i).setFx(1, FxType::Reverb, true, 0.3F, 0.5F, true);
    }
    std::vector<float> l(static_cast<std::size_t>(blockSize));
    std::vector<float> r(static_cast<std::size_t>(blockSize));
    float* out[2] = {l.data(), r.data()};
    for (int i = 0; i < 100; ++i) {
      graph.render(out, 2, blockSize);
    }
    const int blocks = static_cast<int>(8.0 * kRate / blockSize);
    double worst = 0.0;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i) {
      const auto t0 = std::chrono::steady_clock::now();
      graph.render(out, 2, blockSize);
      worst = std::max(worst, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const double load = elapsed / 8.0;
    const double worstLoad = worst / (static_cast<double>(blockSize) / kRate);
    std::printf("[bench] 4 decks keylock+shift+echo+reverb+glue+limiter @48k/%d: average %.1f %%, worst block %.1f %% of the deadline\n",
                blockSize, load * 100.0, worstLoad * 100.0);
    CHECK(isFinite(l));
#ifdef NDEBUG
    CHECK(load < 0.30);
#endif
  }
}

// Hidden by default (run with "[.soak]"): randomised DJ session on 4 decks, offline, to look for overruns and glitches.
// ZYRON_SOAK_SECONDS (default 120) is the amount of audio rendered.
TEST_CASE("AudioGraph: randomised 4-deck soak with every DJ-sound feature", "[.soak][dj-sound]") {
  using namespace zyron::core;
  const double seconds = soakSeconds();
  constexpr int kBlock = 128;

  AudioGraph graph;
  graph.prepare(kRate);
  for (std::size_t i = 0; i < kDeckCount; ++i) {
    const auto mono = generateClickTrack(kRate, 120.0 + 6.0 * static_cast<double>(i), 240.0, 0.0);
    auto track = std::make_shared<TrackBuffer>(2, static_cast<std::int64_t>(mono.size()), kRate);
    std::copy(mono.begin(), mono.end(), track->channelData(0));
    std::copy(mono.begin(), mono.end(), track->channelData(1));
    graph.deck(i).loadTrack(track);
    graph.deck(i).play();
  }
  DeckPlayer::StemBuffers stems;
  for (auto& s : stems) {
    s = sineTrack(110.0, 240.0, 0.1F);
  }
  graph.deck(1).loadStems(stems);

  std::mt19937 rng(2026);
  const auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
  const auto frac = [&]() { return static_cast<float>(rng() % 1000) / 999.0F; };
  const FxType types[] = {FxType::Echo, FxType::Reverb, FxType::Flanger, FxType::Phaser, FxType::Delay, FxType::None};

  std::vector<float> l(kBlock);
  std::vector<float> r(kBlock);
  float* out[2] = {l.data(), r.data()};
  const double deadline = static_cast<double>(kBlock) / kRate;
  const auto blocks = static_cast<long>(seconds * kRate / kBlock);
  long overruns = 0;
  long slow = 0;
  double worst = 0.0;
  bool allFinite = true;
  float peak = 0.0F;
  for (long b = 0; b < blocks; ++b) {
    if (b % 40 == 0) {
      const auto deck = static_cast<DeckId>(pick(4));
      switch (pick(12)) {
        case 0: (void)graph.bridge().pushCommand(SetFx{deck, pick(2), types[pick(6)], pick(4) != 0, frac(), frac(), pick(3) != 0}); break;
        case 1: (void)graph.bridge().pushCommand(SetKeyShift{deck, frac() * 12.0F - 6.0F}); break;
        case 2: (void)graph.bridge().pushCommand(SetKeylock{deck, pick(2) == 0}); break;
        case 3: (void)graph.bridge().pushCommand(SetPlaybackSpeed{deck, 0.85 + 0.3 * static_cast<double>(frac())}); break;
        case 4: (void)graph.bridge().pushCommand(Seek{deck, 5.0 + 100.0 * static_cast<double>(frac())}); break;
        case 5: (void)graph.bridge().pushCommand(SetLoop{deck, 10.0, 10.0 + 0.1 + 4.0 * static_cast<double>(frac()), pick(2) == 0}); break;
        case 6: (void)graph.bridge().pushCommand(Scratch{deck, static_cast<ScratchPattern>(pick(11)), 1.0, 0.5}); break;
        case 7: (void)graph.bridge().pushCommand(SetTrackGainTrim{deck, frac() * 12.0F - 6.0F}); break;
        case 8: (void)graph.bridge().pushCommand(SetCrossfader{frac() * 2.0F - 1.0F}); break;
        case 9: (void)graph.bridge().pushCommand(SetVolume{deck, frac()}); break;
        case 10: (void)graph.bridge().pushCommand(SetFxTempo{deck, 0.3 + 0.3 * static_cast<double>(frac())}); break;
        default: (void)graph.bridge().pushCommand(SetMasterProcessing{pick(5) != 0, true}); break;
      }
    }
    const auto t0 = std::chrono::steady_clock::now();
    graph.render(out, 2, kBlock);
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    worst = std::max(worst, dt);
    overruns += dt > deadline ? 1 : 0;
    slow += dt > 0.5 * deadline ? 1 : 0;
    for (int i = 0; i < kBlock; ++i) {
      allFinite = allFinite && std::isfinite(l[static_cast<std::size_t>(i)]) && std::isfinite(r[static_cast<std::size_t>(i)]);
      peak = std::max({peak, std::abs(l[static_cast<std::size_t>(i)]), std::abs(r[static_cast<std::size_t>(i)])});
    }
  }
  std::printf("[soak] %.0f s of audio, %ld blocks of %d: overruns %ld, blocks over 50 %% of the deadline %ld, worst block %.1f %% of the deadline, "
              "peak %.4f\n", seconds, blocks, kBlock, overruns, slow, worst / deadline * 100.0, static_cast<double>(peak));
  CHECK(allFinite);
  CHECK(peak <= 1.0F);
}
