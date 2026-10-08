// SPDX-License-Identifier: AGPL-3.0-only
// Integrated loudness (BS.1770 / R128), the per-bar energy profile and the vocal-region heuristic, on synthetic signals.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include "Analysis/Loudness/LoudnessMeter.hpp"
#include "Analysis/Structure/BarProfile.hpp"
#include "Analysis/Structure/VocalDetector.hpp"

using Catch::Approx;
using namespace zyron::analysis;

namespace {

constexpr double kPi = std::numbers::pi;

std::vector<float> sine(double hz, double dbfsPeak, double seconds, int rate) {
  std::vector<float> x(static_cast<std::size_t>(seconds * rate));
  const double amp = std::pow(10.0, dbfsPeak / 20.0);
  for (std::size_t i = 0; i < x.size(); ++i) {
    x[i] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
  }
  return x;
}

/// A drum-and-bass-like bed: kick on every beat, sub bass, hats. 170 BPM.
std::vector<float> bed(double seconds, int rate, std::uint32_t seed = 1) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> noise(-1.0F, 1.0F);
  std::vector<float> x(static_cast<std::size_t>(seconds * rate), 0.0F);
  const double beat = 60.0 / 170.0;
  float hatPrev = 0.0F;
  for (std::size_t i = 0; i < x.size(); ++i) {
    const double t = static_cast<double>(i) / rate;
    const double inBeat = std::fmod(t, beat);
    const double kick = inBeat < 0.12 ? std::exp(-inBeat * 30.0) * std::sin(2.0 * kPi * (50.0 + 80.0 * std::exp(-inBeat * 40.0)) * inBeat) : 0.0;
    const double bass = 0.25 * std::sin(2.0 * kPi * 55.0 * t);
    const float n = noise(rng);
    const float hat = n - hatPrev;  // crude high-pass
    hatPrev = n;
    const double hatEnv = std::fmod(t, beat / 2.0) < 0.03 ? 1.0 : 0.0;
    x[i] = static_cast<float>(0.5 * kick + bass + 0.08 * hat * hatEnv);
  }
  return x;
}

/// A sung line: harmonic source (f0 ~200 Hz with vibrato and a slow melody), two formants that change vowel every
/// 0.3 s, syllable amplitude modulation. Added on top of `x` between `from` and `to` seconds.
void addVoice(std::vector<float>& x, int rate, double from, double to, double level) {
  static constexpr double kVowels[5][2] = {{700, 1200}, {400, 2000}, {300, 2500}, {500, 900}, {350, 700}};
  double phase[24] = {};
  for (std::size_t i = static_cast<std::size_t>(from * rate); i < std::min(x.size(), static_cast<std::size_t>(to * rate)); ++i) {
    const double t = static_cast<double>(i) / rate;
    const int vowel = static_cast<int>(t / 0.3) % 5;
    const double f0 = 200.0 * (1.0 + 0.12 * std::sin(2.0 * kPi * 0.7 * t)) * (1.0 + 0.02 * std::sin(2.0 * kPi * 5.5 * t));
    const double syllable = 0.55 + 0.45 * std::sin(2.0 * kPi * 3.5 * t);
    double sample = 0.0;
    for (int h = 1; h <= 20; ++h) {
      phase[h] += 2.0 * kPi * f0 * h / rate;
      const double f = f0 * h;
      const double g = std::exp(-std::pow((f - kVowels[vowel][0]) / 150.0, 2.0)) +
                       0.7 * std::exp(-std::pow((f - kVowels[vowel][1]) / 250.0, 2.0)) + 0.02;
      sample += g * std::sin(phase[h]) / h;
    }
    x[i] += static_cast<float>(level * syllable * sample);
  }
}

/// A held, motionless polyphonic pad and a static mono saw lead: harmonic, but not a voice.
void addPad(std::vector<float>& x, int rate, double from, double to, double level) {
  for (std::size_t i = static_cast<std::size_t>(from * rate); i < std::min(x.size(), static_cast<std::size_t>(to * rate)); ++i) {
    const double t = static_cast<double>(i) / rate;
    double s = 0.0;
    for (const double f0 : {130.8, 164.8, 196.0, 261.6}) {
      for (int h = 1; h <= 12; ++h) {
        s += std::sin(2.0 * kPi * f0 * h * t) / h;
      }
    }
    x[i] += static_cast<float>(level * s * 0.25);
  }
}

void addStaticLead(std::vector<float>& x, int rate, double from, double to, double level) {
  for (std::size_t i = static_cast<std::size_t>(from * rate); i < std::min(x.size(), static_cast<std::size_t>(to * rate)); ++i) {
    const double t = static_cast<double>(i) / rate;
    double s = 0.0;
    for (int h = 1; h <= 20; ++h) {
      s += std::sin(2.0 * kPi * 220.0 * h * t) / h;
    }
    x[i] += static_cast<float>(level * s);
  }
}

bool covers(const std::vector<VocalRegion>& regions, double from, double to, double minShare) {
  double covered = 0.0;
  for (const auto& r : regions) {
    covered += std::max(0.0, std::min(r.endSec, to) - std::max(r.startSec, from));
  }
  return covered >= minShare * (to - from);
}

double overlap(const std::vector<VocalRegion>& regions, double from, double to) {
  double covered = 0.0;
  for (const auto& r : regions) {
    covered += std::max(0.0, std::min(r.endSec, to) - std::max(r.startSec, from));
  }
  return covered;
}

}  // namespace

TEST_CASE("Loudness: K-weighting coefficients at 48 kHz equal the BS.1770-4 tables", "[analysis][loudness]") {
  const auto k = kWeightingCoefficients(48000.0);
  CHECK(k.shelfB[0] == Approx(1.53512485958697).epsilon(1e-6));
  CHECK(k.shelfB[1] == Approx(-2.69169618940638).epsilon(1e-6));
  CHECK(k.shelfB[2] == Approx(1.19839281085285).epsilon(1e-6));
  CHECK(k.shelfA[0] == Approx(-1.69065929318241).epsilon(1e-6));
  CHECK(k.shelfA[1] == Approx(0.73248077421585).epsilon(1e-6));
  CHECK(k.highPassA[0] == Approx(-1.99004745483398).epsilon(1e-6));
  CHECK(k.highPassA[1] == Approx(0.99007225036621).epsilon(1e-6));
}

TEST_CASE("Loudness: a 1 kHz sine at -20 dBFS peak", "[analysis][loudness]") {
  // BS.1770: a 997 Hz sine of -20 dBFS peak in BOTH channels is -20.0 LUFS (the -0.691 offset cancels the K filter's
  // +0.69 dB there); in ONE channel only the power halves: -23.0 LUFS. The decoder hands mono out as two equal
  // channels, so a mono file measures -20.0.
  for (const int rate : {44100, 48000}) {
    const auto x = sine(1000.0, -20.0, 10.0, rate);
    const float* both[2] = {x.data(), x.data()};
    const auto dual = measureIntegratedLoudness(both, 2, x.size(), rate);
    REQUIRE(dual.valid);
    CHECK(dual.lufs == Approx(-20.0).margin(0.1));

    const auto single = measureIntegratedLoudness(both, 1, x.size(), rate);
    REQUIRE(single.valid);
    CHECK(single.lufs == Approx(-23.0).margin(0.1));
  }
}

TEST_CASE("Loudness: level scales 1:1 and the gates ignore silence", "[analysis][loudness]") {
  const int rate = 44100;
  auto loud = sine(1000.0, -14.0, 6.0, rate);
  const float* ch[2] = {loud.data(), loud.data()};
  const double reference = measureIntegratedLoudness(ch, 2, loud.size(), rate).lufs;
  CHECK(reference == Approx(-14.0).margin(0.1));

  // 20 s of digital silence after the music change the integrated value by only the one block that straddles the
  // edge (all-silent blocks fall under the absolute gate).
  loud.resize(loud.size() + static_cast<std::size_t>(20 * rate), 0.0F);
  const float* padded[2] = {loud.data(), loud.data()};
  CHECK(measureIntegratedLoudness(padded, 2, loud.size(), rate).lufs == Approx(reference).margin(0.2));

  // A quiet passage (-30 LU below) is removed by the relative gate: only the loud half counts.
  auto mixed = sine(1000.0, -14.0, 5.0, rate);
  const auto quiet = sine(1000.0, -44.0, 5.0, rate);
  mixed.insert(mixed.end(), quiet.begin(), quiet.end());
  const float* m[2] = {mixed.data(), mixed.data()};
  CHECK(measureIntegratedLoudness(m, 2, mixed.size(), rate).lufs == Approx(-14.0).margin(0.15));
}

TEST_CASE("Loudness: silence and too-short input are not valid", "[analysis][loudness]") {
  const int rate = 44100;
  std::vector<float> silence(static_cast<std::size_t>(5 * rate), 0.0F);
  const float* s[1] = {silence.data()};
  CHECK_FALSE(measureIntegratedLoudness(s, 1, silence.size(), rate).valid);
  const auto brief = sine(1000.0, -10.0, 0.3, rate);
  const float* b[1] = {brief.data()};
  CHECK_FALSE(measureIntegratedLoudness(b, 1, brief.size(), rate).valid);
  CHECK_FALSE(measureIntegratedLoudness(nullptr, 0, 0, rate).valid);
}

TEST_CASE("Loudness: pink-ish noise reads the same in both channels as in one at +3.01 LU", "[analysis][loudness]") {
  const int rate = 44100;
  std::mt19937 rng(7);
  std::normal_distribution<float> gauss(0.0F, 0.05F);
  std::vector<float> x(static_cast<std::size_t>(10 * rate));
  float b0 = 0, b1 = 0, b2 = 0;  // Paul Kellet's pink filter
  for (auto& v : x) {
    const float w = gauss(rng);
    b0 = 0.99765F * b0 + w * 0.0990460F;
    b1 = 0.96300F * b1 + w * 0.2965164F;
    b2 = 0.57000F * b2 + w * 1.0526913F;
    v = b0 + b1 + b2 + w * 0.1848F;
  }
  const float* both[2] = {x.data(), x.data()};
  const double dual = measureIntegratedLoudness(both, 2, x.size(), rate).lufs;
  const double single = measureIntegratedLoudness(both, 1, x.size(), rate).lufs;
  CHECK(dual - single == Approx(3.0103).margin(0.01));
}

TEST_CASE("Loudness: 7 minutes take well under a second", "[analysis][loudness][perf]") {
  const int rate = 44100;
  const auto x = sine(1000.0, -18.0, 7 * 60.0, rate);
  const float* ch[2] = {x.data(), x.data()};
  const auto t0 = std::chrono::steady_clock::now();
  const auto r = measureIntegratedLoudness(ch, 2, x.size(), rate);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  INFO("loudness of 7 min stereo: " << ms << " ms");
  CHECK(r.valid);
  CHECK(ms < 1000.0);
}

TEST_CASE("BarProfile: quiet breakdown reads lower than the drop and the text form round-trips", "[analysis][barprofile]") {
  const int rate = 22050;
  const double bar = 4.0 * 60.0 / 170.0;
  std::vector<float> x;
  const auto loud = sine(100.0, -6.0, 8 * bar + 0.001, rate);
  const auto soft = sine(100.0, -18.0, 8 * bar + 0.001, rate);
  x.insert(x.end(), loud.begin(), loud.end());
  x.insert(x.end(), soft.begin(), soft.end());
  x.insert(x.end(), loud.begin(), loud.end());

  BarProfileInput in;
  in.mono = x.data();
  in.frames = x.size();
  in.sampleRate = rate;
  in.originSec = 0.0;
  in.barSec = bar;
  in.endSec = static_cast<double>(x.size()) / rate;
  const BarProfile profile = computeBarProfile(in);
  REQUIRE(profile.energy.size() == 24);
  REQUIRE(profile.bass.size() == 24);
  CHECK(profile.energy[3] == Approx(1.0F).margin(0.02));
  CHECK(profile.energy[11] == Approx(1.0F - 12.0F / 30.0F).margin(0.05));  // 12 dB under the loud level
  CHECK(profile.energy[20] > profile.energy[11]);

  const std::string text = profile.encode();
  BarProfile back;
  REQUIRE(BarProfile::decode(text, back));
  CHECK(back.barSec == Approx(bar).margin(1e-5));
  REQUIRE(back.energy.size() == 24);
  for (std::size_t i = 0; i < 24; ++i) {
    CHECK(back.energy[i] == Approx(profile.energy[i]).margin(1.0 / 255.0));
    CHECK(back.bass[i] == Approx(profile.bass[i]).margin(1.0 / 255.0));
  }
  CHECK(text.size() < 20 + 4 * 24 + 10);

  BarProfile untouched;
  untouched.barSec = 9.0;
  CHECK_FALSE(BarProfile::decode("garbage", untouched));
  CHECK_FALSE(BarProfile::decode("ZB1;0;1.0;3;00;00;00", untouched));    // wrong lengths
  CHECK_FALSE(BarProfile::decode("ZB1;0;1.0;2;00ZZ;0000", untouched));   // not hex
  CHECK_FALSE(BarProfile::decode("ZB1;0;-1.0;1;00;00", untouched));      // bad bar length
  CHECK(untouched.barSec == 9.0);
}

TEST_CASE("BarProfile: silence and short input give an empty profile", "[analysis][barprofile]") {
  std::vector<float> silence(44100 * 10, 0.0F);
  BarProfileInput in{silence.data(), silence.size(), 44100, 0.0, 1.4, 10.0};
  const BarProfile p = computeBarProfile(in);
  REQUIRE_FALSE(p.empty());
  CHECK(*std::max_element(p.energy.begin(), p.energy.end()) == 0.0F);
  in.endSec = 1.0;
  CHECK(computeBarProfile(in).empty());
  CHECK(BarProfile{}.encode().empty());
}

TEST_CASE("VocalDetector: finds a sung line over a drum and bass bed", "[analysis][vocals]") {
  const int rate = 44100;
  const double bar = 4.0 * 60.0 / 170.0;
  const int bars = 40;
  auto x = bed(bars * bar, rate);
  const double from = 12 * bar;
  const double to = 24 * bar;
  addVoice(x, rate, from, to, 0.35);

  const VocalGrid grid{0.0, bar, bars};
  const auto regions = detectVocals(x.data(), x.size(), rate, grid);
  INFO("regions: " << regions.size());
  for (const auto& r : regions) {
    INFO(r.startSec << " - " << r.endSec);
  }
  CHECK(covers(regions, from, to, 0.7));
  CHECK(overlap(regions, 0.0, from - 2 * bar) < 1.0);
  CHECK(overlap(regions, to + 2 * bar, bars * bar) < 1.0);
}

TEST_CASE("VocalDetector: a pad, a static lead and a bare bed are not vocals", "[analysis][vocals]") {
  const int rate = 44100;
  const double bar = 4.0 * 60.0 / 170.0;
  const int bars = 32;
  const VocalGrid grid{0.0, bar, bars};

  auto bare = bed(bars * bar, rate, 3);
  CHECK(detectVocals(bare.data(), bare.size(), rate, grid).empty());

  auto pad = bed(bars * bar, rate, 4);
  addPad(pad, rate, 4 * bar, 28 * bar, 0.5);
  CHECK(overlap(detectVocals(pad.data(), pad.size(), rate, grid), 0.0, bars * bar) < 2.0 * bar);

  auto lead = bed(bars * bar, rate, 5);
  addStaticLead(lead, rate, 4 * bar, 28 * bar, 0.2);
  CHECK(overlap(detectVocals(lead.data(), lead.size(), rate, grid), 0.0, bars * bar) < 2.0 * bar);
}

TEST_CASE("VocalDetector: degenerate input is safe", "[analysis][vocals]") {
  const VocalGrid grid{0.0, 1.4, 4};
  CHECK(detectVocals(nullptr, 0, 44100, grid).empty());
  std::vector<float> tiny(100, 0.1F);
  CHECK(detectVocals(tiny.data(), tiny.size(), 44100, grid).empty());
  std::vector<float> silence(44100 * 6, 0.0F);
  CHECK(detectVocals(silence.data(), silence.size(), 44100, grid).empty());
  CHECK(detectVocals(silence.data(), silence.size(), 44100, VocalGrid{0.0, 0.0, 0}).empty());
}

TEST_CASE("VocalDetector: the vocal stem decides by bar level", "[analysis][vocals]") {
  const int rate = 44100;
  const double bar = 2.0;
  std::vector<float> stem(static_cast<std::size_t>(20 * bar * rate), 0.0F);
  for (std::size_t i = static_cast<std::size_t>(6 * bar * rate); i < static_cast<std::size_t>(14 * bar * rate); ++i) {
    stem[i] = 0.2F * std::sin(static_cast<float>(i) * 0.05F);
  }
  // Leak of the other instruments in the quiet part (-60 dBFS): below the floor.
  for (std::size_t i = 0; i < static_cast<std::size_t>(6 * bar * rate); ++i) {
    stem[i] = 0.001F * std::sin(static_cast<float>(i) * 0.03F);
  }
  const auto regions = vocalsFromStem(stem.data(), stem.size(), rate, VocalGrid{0.0, bar, 20});
  REQUIRE(regions.size() == 1);
  CHECK(regions[0].startSec == Approx(6 * bar));
  CHECK(regions[0].endSec == Approx(14 * bar));
}

TEST_CASE("VocalDetector: 7 minutes of audio take well under a second", "[analysis][vocals][perf]") {
  const int rate = 44100;
  const double bar = 4.0 * 60.0 / 170.0;
  const int bars = static_cast<int>(7 * 60.0 / bar);
  auto x = bed(bars * bar, rate);
  addVoice(x, rate, 30.0, 200.0, 0.3);
  const auto t0 = std::chrono::steady_clock::now();
  const auto regions = detectVocals(x.data(), x.size(), rate, VocalGrid{0.0, bar, bars});
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  INFO("vocal detection of 7 min: " << ms << " ms");
  CHECK_FALSE(regions.empty());
  CHECK(ms < 1000.0);
}
