// SPDX-License-Identifier: AGPL-3.0-only
// The performance layer: synthesized DJ hits (TriggerFxHit) mixed into the master bus in front of glue and limiter.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "Audio/Bridge/CommandBridge.hpp"
#include "Audio/DSP/FxHitPlayer.hpp"
#include "Audio/DSP/Mixer.hpp"
#include "Audio/Engine/AudioGraph.hpp"
#include "Core/Commands/Command.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron;
using namespace zyron::audio;
using namespace zyron::core;
using namespace zyron::test;

namespace {

constexpr double kRate = 48000.0;
constexpr FxHitType kAllTypes[] = {FxHitType::AirHorn, FxHitType::Siren,  FxHitType::Riser,
                                   FxHitType::Downlifter, FxHitType::Impact, FxHitType::Laser};

struct Stereo {
  std::vector<float> l;
  std::vector<float> r;
};

/// Renders `samples` frames of the player (summed onto silence) in blocks of `block`.
Stereo render(FxHitPlayer& player, std::int64_t samples, int block) {
  Stereo out{std::vector<float>(static_cast<std::size_t>(samples), 0.0F),
             std::vector<float>(static_cast<std::size_t>(samples), 0.0F)};
  for (std::int64_t done = 0; done < samples; done += block) {
    const int n = static_cast<int>(std::min<std::int64_t>(block, samples - done));
    player.process(out.l.data() + done, out.r.data() + done, n);
  }
  return out;
}

float peakOf(const Stereo& s) {
  return std::max(maxAbsolute(s.l), maxAbsolute(s.r));
}

}  // namespace

TEST_CASE("TriggerFxHit command: name, validation, state, bridge", "[core][command][fx-hit]") {
  const AppState state{};
  CHECK(commandName(TriggerFxHit{}) == "TRIGGER_FX_HIT");
  CHECK_FALSE(targetDeck(TriggerFxHit{}).has_value());

  CHECK_FALSE(validate(TriggerFxHit{}, state).has_value());  // defaults: air horn, 0.5, free
  CHECK_FALSE(validate(TriggerFxHit{FxHitType::Laser, 1.0F, 0.5}, state).has_value());
  CHECK_FALSE(validate(TriggerFxHit{FxHitType::Riser, 0.0F, 0.0}, state).has_value());
  CHECK(validate(TriggerFxHit{FxHitType::Riser, 1.01F, 0.0}, state).has_value());
  CHECK(validate(TriggerFxHit{FxHitType::Riser, -0.1F, 0.0}, state).has_value());
  CHECK(validate(TriggerFxHit{FxHitType::Riser, 0.5F, 0.01}, state).has_value());   // faster than 400 BPM
  CHECK(validate(TriggerFxHit{FxHitType::Riser, 0.5F, 10.0}, state).has_value());   // slower than 20 BPM
  CHECK(validate(TriggerFxHit{FxHitType::Riser, 0.5F, -1.0}, state).has_value());
  CHECK(validate(TriggerFxHit{static_cast<FxHitType>(6), 0.5F, 0.0}, state).has_value());

  const AppState next = apply(state, TriggerFxHit{FxHitType::Impact, 0.8F, 0.5});
  CHECK(next.revision == state.revision + 1);
  CHECK(next.mixer.crossfader == state.mixer.crossfader);
  CHECK(next.decks[0].playing == state.decks[0].playing);

  const auto msg = CommandBridge::translateCommand(TriggerFxHit{FxHitType::Siren, 0.7F, 0.46875});
  REQUIRE(msg.has_value());
  CHECK(msg->type == RtMessageType::MixerFxHit);
  CHECK(msg->data.fxHit.type == FxHitType::Siren);
  CHECK(msg->data.fxHit.level == 0.7F);
  CHECK(msg->data.fxHit.beatSeconds == 0.46875);
}

TEST_CASE("Every hit renders finite audio within its length, with soft start and end", "[audio][fx-hit]") {
  for (const FxHitType type : kAllTypes) {
    for (const double beat : {0.0, 0.5, 0.46875, 0.3}) {
      DYNAMIC_SECTION("type " << static_cast<int>(type) << " beat " << beat) {
        FxHitPlayer player;
        player.prepare(kRate);
        player.trigger(type, 1.0F, beat);
        REQUIRE(player.activeVoices() == 1);

        const std::int64_t length = FxHitPlayer::hitLengthSamples(type, beat, kRate);
        const std::int64_t total = length + 4800;
        const Stereo out = render(player, total, 256);

        CHECK(isFinite(out.l));
        CHECK(isFinite(out.r));
        const float peak = peakOf(out);
        INFO("peak " << peak << " type " << static_cast<int>(type));
        std::printf("fxhit type %d beat %.3f peak %.3f\n", static_cast<int>(type), beat, static_cast<double>(peak));
        CHECK(peak > 0.3F);  // about -6 dBFS at level 1 (-10 .. -4.4 dB accepted)
        CHECK(peak < 0.6F);

        // Soft start and end: no step.
        CHECK(std::abs(out.l.front()) < 1e-3F);
        CHECK(std::abs(out.r.front()) < 1e-3F);
        CHECK(std::abs(out.l[static_cast<std::size_t>(length - 1)]) < 1e-3F);
        CHECK(std::abs(out.r[static_cast<std::size_t>(length - 1)]) < 1e-3F);
        // Silent after the length, and the voice has freed itself.
        CHECK(maxAbsolute(out.l, static_cast<std::size_t>(length)) == 0.0F);
        CHECK(player.activeVoices() == 0);
      }
    }
  }
}

TEST_CASE("Hit lengths follow the beat", "[audio][fx-hit]") {
  const auto lenAt = [](FxHitType t, double beat) { return FxHitPlayer::hitLengthSamples(t, beat, kRate); };
  CHECK(lenAt(FxHitType::Riser, 0.5) == 4 * 24000 / 1);  // 4 beats of 0.5 s
  CHECK(lenAt(FxHitType::Riser, 0.25) * 2 == lenAt(FxHitType::Riser, 0.5));
  CHECK(lenAt(FxHitType::AirHorn, 0.0) == lenAt(FxHitType::AirHorn, FxHitPlayer::kDefaultBeatSeconds));
  CHECK(lenAt(FxHitType::Siren, 3.0) <= static_cast<std::int64_t>(FxHitPlayer::kMaxLengthSeconds * kRate));
}

TEST_CASE("Hits are identical for every block size", "[audio][fx-hit]") {
  for (const FxHitType type : kAllTypes) {
    FxHitPlayer reference;
    reference.prepare(kRate);
    reference.trigger(type, 0.9F, 0.5);
    const std::int64_t total = FxHitPlayer::hitLengthSamples(type, 0.5, kRate) + 100;
    const Stereo expected = render(reference, total, 480);

    for (const int block : {32, 64, 100, 512, 1024, 2048}) {
      FxHitPlayer player;
      player.prepare(kRate);
      player.trigger(type, 0.9F, 0.5);
      const Stereo got = render(player, total, block);
      INFO("type " << static_cast<int>(type) << " block " << block);
      CHECK(nullTestMaxDiff(expected.l, got.l) == 0.0F);
      CHECK(nullTestMaxDiff(expected.r, got.r) == 0.0F);
    }
  }
}

TEST_CASE("Air horn stabs sit on the beat within 1 ms", "[audio][fx-hit]") {
  for (const double beat : {0.5, 0.46875, 0.4, 0.75}) {
    FxHitPlayer player;
    player.prepare(kRate);
    player.trigger(FxHitType::AirHorn, 1.0F, beat);
    const Stereo out = render(player, FxHitPlayer::hitLengthSamples(FxHitType::AirHorn, beat, kRate), 256);

    // Stab onsets: the first sample above 0.3% of the peak after at least 10 ms below it.
    const float threshold = 0.003F * maxAbsolute(out.l);
    constexpr std::size_t kGap = 480;  // 10 ms
    std::vector<double> onsets;
    std::size_t lastAbove = 0;
    for (std::size_t i = 0; i < out.l.size(); ++i) {
      if (std::abs(out.l[i]) > threshold) {
        if (onsets.empty() || i - lastAbove > kGap) onsets.push_back(static_cast<double>(i) / kRate);
        lastAbove = i;
      }
    }
    INFO("beat " << beat);
    REQUIRE(onsets.size() == 4);  // three short stabs and one long
    for (int k = 0; k < 4; ++k) {
      const double expected = 0.5 * k * beat;
      CHECK(std::abs(onsets[static_cast<std::size_t>(k)] - expected) < 0.001);
    }
  }
}

TEST_CASE("A fifth hit steals the oldest voice without a click", "[audio][fx-hit]") {
  constexpr int kLead = 4800;
  constexpr int kTail = 4800;
  const auto run = [&](int sirens) {
    FxHitPlayer player;
    player.prepare(kRate);
    for (int i = 0; i < sirens; ++i) player.trigger(FxHitType::Siren, 0.5F, 0.5);
    Stereo out{std::vector<float>(kLead + kTail, 0.0F), std::vector<float>(kLead + kTail, 0.0F)};
    player.process(out.l.data(), out.r.data(), kLead);
    if (sirens == FxHitPlayer::kMaxVoices) CHECK(player.activeVoices() == FxHitPlayer::kMaxVoices);
    player.trigger(FxHitType::Laser, 0.5F, 0.5);
    CHECK(player.activeVoices() == FxHitPlayer::kMaxVoices);  // sirens (one stolen) + the new laser
    player.process(out.l.data() + kLead, out.r.data() + kLead, kTail);
    return out;
  };
  const Stereo stolen = run(FxHitPlayer::kMaxVoices);      // 4 sirens, the laser steals the oldest
  const Stereo control = run(FxHitPlayer::kMaxVoices - 1);  // the same without the voice that gets stolen
  REQUIRE(isFinite(stolen.l));

  // stolen - control is exactly the stolen siren under its fade-out: it must be continuous and end in silence.
  const std::size_t fadeEnd = kLead + static_cast<std::size_t>(FxHitPlayer::kStealFadeSeconds * kRate) + 2;
  float maxStep = 0.0F;
  float prev = stolen.l[kLead - 1] - control.l[kLead - 1];
  for (std::size_t i = kLead; i < stolen.l.size(); ++i) {
    const float d = stolen.l[i] - control.l[i];
    maxStep = std::max(maxStep, std::abs(d - prev));
    prev = d;
    if (i >= fadeEnd) CHECK(std::abs(d) < 1e-5F);
  }
  INFO("max step of the stolen voice " << maxStep);
  CHECK(maxStep < 0.12F);  // the siren's own square edges step ~0.09; an abrupt cut would step by its level (~0.25)

  // A burst of triggers never exceeds the voice budget nor produces non-finite output.
  FxHitPlayer player;
  player.prepare(kRate);
  std::vector<float> l(16, 0.0F);
  std::vector<float> r(16, 0.0F);
  for (int i = 0; i < 60; ++i) {
    player.trigger(static_cast<FxHitType>(i % 6), 1.0F, 0.0);
    CHECK(player.activeVoices() <= FxHitPlayer::kMaxVoices);
    player.process(l.data(), r.data(), 16);
    CHECK(isFinite(l));
    CHECK(isFinite(r));
  }
}

TEST_CASE("Level zero and invalid input start nothing", "[audio][fx-hit]") {
  FxHitPlayer player;
  player.prepare(kRate);
  player.trigger(FxHitType::Impact, 0.0F, 0.0);
  player.trigger(static_cast<FxHitType>(99), 1.0F, 0.0);
  player.trigger(FxHitType::Impact, std::nanf(""), 0.0);
  CHECK(player.activeVoices() == 0);
  player.trigger(FxHitType::Impact, 5.0F, std::nan(""));  // level clamps, beat falls back to free
  CHECK(player.activeVoices() == 1);
}

TEST_CASE("Level scales the hit", "[audio][fx-hit]") {
  const auto peakAt = [](float level) {
    FxHitPlayer player;
    player.prepare(kRate);
    player.trigger(FxHitType::Laser, level, 0.5);
    return peakOf(render(player, 30000, 256));
  };
  CHECK(peakAt(0.5F) == Catch::Approx(peakAt(1.0F) * 0.5F).margin(1e-4));
}

TEST_CASE("TriggerFxHit through the graph: no allocation, output stays under the limiter ceiling", "[audio][fx-hit][graph]") {
  AudioGraph graph;
  graph.prepare(kRate);
  graph.bridge().pushCommand(SetTestTone{false});
  std::vector<float> l(256);
  std::vector<float> r(256);
  float* out[2] = {l.data(), r.data()};
  graph.render(out, 2, 256);

  for (const FxHitType type : kAllTypes) {
    REQUIRE(graph.bridge().pushCommand(TriggerFxHit{type, 1.0F, 0.0}));
  }
  const float ceiling = std::pow(10.0F, -0.3F / 20.0F) + 0.002F;
  float peak = 0.0F;
  {
    ScopedRealtimeGuard guard;
    for (int block = 0; block < 400; ++block) {
      graph.render(out, 2, 256);
      for (int i = 0; i < 256; ++i) {
        peak = std::max({peak, std::abs(l[static_cast<std::size_t>(i)]), std::abs(r[static_cast<std::size_t>(i)])});
        if (!std::isfinite(l[static_cast<std::size_t>(i)])) FAIL("non-finite output");
      }
    }
    CHECK_FALSE(guard.hasViolations());
  }
  INFO("peak " << peak);
  CHECK(peak > 0.1F);  // the hits are audible on an otherwise silent master
  CHECK(peak <= ceiling);
}

TEST_CASE("A hit over a loud master stays within the limiter ceiling", "[audio][fx-hit][mixer]") {
  Mixer mixer;
  mixer.prepare(kRate);
  const std::vector<float> loud = generateSine(kRate, 110.0, 4.0, 0.95F);
  const auto n = static_cast<int>(loud.size());
  std::vector<float> outL(loud.size());
  std::vector<float> outR(loud.size());
  const float* inL[1] = {loud.data()};
  const float* inR[1] = {loud.data()};

  mixer.setChannelAssign(0, CrossfaderAssign::Thru);
  mixer.fxHits().trigger(FxHitType::Impact, 1.0F, 0.5);
  mixer.fxHits().trigger(FxHitType::AirHorn, 1.0F, 0.5);
  for (int done = 0; done < n; done += 256) {
    const int len = std::min(256, n - done);
    const float* l[1] = {inL[0] + done};
    const float* rr[1] = {inR[0] + done};
    mixer.process(l, rr, 1, outL.data() + done, outR.data() + done, len);
  }
  CHECK(isFinite(outL));
  const float ceiling = std::pow(10.0F, -0.3F / 20.0F) + 0.002F;
  CHECK(maxAbsolute(outL) <= ceiling);
  CHECK(maxAbsolute(outR) <= ceiling);
}
