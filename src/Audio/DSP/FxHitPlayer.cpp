// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/FxHitPlayer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace zyron::audio {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kMinBeat = 0.15;
constexpr double kMaxBeat = 3.0;

// Length of each hit in beats.
constexpr double kAirHornBeats = 3.0;
constexpr double kSirenBeats = 8.0;
constexpr double kRiserBeats = 4.0;
constexpr double kImpactBeats = 6.0;
constexpr double kLaserBeats = 1.0;

// Output calibration: level 1 -> about -6 dBFS peak (0.5), measured by tests/audio/test_fx_hits.cpp.
constexpr float kNormAirHorn = 0.68F;
constexpr float kNormSiren = 0.55F;
constexpr float kNormRiser = 0.46F;
constexpr float kNormDownlifter = 0.46F;
constexpr float kNormImpact = 0.53F;
constexpr float kNormLaser = 0.7F;

constexpr double kAirHornNotesHz[2] = {392.0, 523.25};
constexpr double kAirHornDetune[3] = {0.99596, 1.0, 1.00406};  // about +-7 cents

/// Raised-cosine fade 0 -> 1 over `len` samples at position `pos`.
inline float fadeIn(std::int64_t pos, std::int64_t len) noexcept {
  if (pos <= 0) return 0.0F;
  if (pos >= len) return 1.0F;
  return 0.5F - 0.5F * static_cast<float>(std::cos(std::numbers::pi * static_cast<double>(pos) / static_cast<double>(len)));
}

/// 1 -> 0 over the last `len` samples of a hit of `total` samples (the final sample is exactly 0).
inline float fadeOutAtEnd(std::int64_t n, std::int64_t total, std::int64_t len) noexcept {
  return fadeIn(total - 1 - n, len);
}

inline std::int64_t samplesOf(double seconds, double sampleRate) noexcept {
  return std::max<std::int64_t>(1, std::llround(seconds * sampleRate));
}

inline float nextNoise(std::uint32_t& state) noexcept {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<float>(static_cast<std::int32_t>(state)) * (1.0F / 2147483648.0F);
}

/// Two-sample polynomial band-limiting correction for a discontinuity at phase wrap.
inline float polyBlep(double t, double dt) noexcept {
  if (t < dt) {
    t /= dt;
    return static_cast<float>(t + t - t * t - 1.0);
  }
  if (t > 1.0 - dt) {
    t = (t - 1.0) / dt;
    return static_cast<float>(t * t + t + t + 1.0);
  }
  return 0.0F;
}

inline float sawBlep(double phase, double dt) noexcept {
  return static_cast<float>(2.0 * phase - 1.0) - polyBlep(phase, dt);
}

inline float squareBlep(double phase, double dt) noexcept {
  float v = phase < 0.5 ? 1.0F : -1.0F;
  v += polyBlep(phase, dt);
  double p2 = phase + 0.5;
  if (p2 >= 1.0) p2 -= 1.0;
  v -= polyBlep(p2, dt);
  return v;
}

inline void advance(double& phase, double inc) noexcept {
  phase += inc;
  phase -= std::floor(phase);
}

inline double beatOrDefault(double beatSeconds) noexcept {
  if (!std::isfinite(beatSeconds) || beatSeconds <= 0.0) return FxHitPlayer::kDefaultBeatSeconds;
  return std::clamp(beatSeconds, kMinBeat, kMaxBeat);
}

}  // namespace

void FxHitPlayer::prepare(double sampleRate) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  reset();
}

void FxHitPlayer::reset() noexcept {
  for (auto& v : voices_) {
    v = Voice{};
  }
  nextSerial_ = 1;
}

std::int64_t FxHitPlayer::hitLengthSamples(core::FxHitType type, double beatSeconds, double sampleRate) noexcept {
  const double beat = beatOrDefault(beatSeconds);
  double beats = 0.0;
  switch (type) {
    case core::FxHitType::AirHorn: beats = kAirHornBeats; break;
    case core::FxHitType::Siren: beats = kSirenBeats; break;
    case core::FxHitType::Riser:
    case core::FxHitType::Downlifter: beats = kRiserBeats; break;
    case core::FxHitType::Impact: beats = kImpactBeats; break;
    case core::FxHitType::Laser: beats = kLaserBeats; break;
  }
  return samplesOf(std::min(beats * beat, kMaxLengthSeconds), sampleRate);
}

int FxHitPlayer::activeVoices() const noexcept {
  int count = 0;
  for (const auto& v : voices_) {
    if (v.used && !v.dying) ++count;
  }
  return count;
}

void FxHitPlayer::trigger(core::FxHitType type, float level, double beatSeconds) noexcept {
  if (!core::isValid(type) || !std::isfinite(level)) return;
  level = std::clamp(level, 0.0F, 1.0F);
  if (level <= 0.0F) return;

  // Find a free slot; count the audible voices and remember the oldest one.
  Voice* freeSlot = nullptr;
  Voice* oldest = nullptr;
  int audible = 0;
  for (auto& v : voices_) {
    if (!v.used) {
      if (freeSlot == nullptr) freeSlot = &v;
    } else if (!v.dying) {
      ++audible;
      if (oldest == nullptr || v.serial < oldest->serial) oldest = &v;
    }
  }
  if (audible >= kMaxVoices && oldest != nullptr) {
    // Steal the oldest: it fades out over a few ms instead of being cut.
    oldest->dying = true;
    oldest->stealGain = 1.0F;
    oldest->stealStep = 1.0F / static_cast<float>(samplesOf(kStealFadeSeconds, sampleRate_));
  }
  if (freeSlot == nullptr) {
    // Every slot busy (a burst of triggers inside the steal fade): reuse the dying voice that is quietest.
    for (auto& v : voices_) {
      if (v.dying && (freeSlot == nullptr || v.stealGain < freeSlot->stealGain)) freeSlot = &v;
    }
  }
  if (freeSlot != nullptr) startVoice(*freeSlot, type, level, beatOrDefault(beatSeconds));
}

void FxHitPlayer::startVoice(Voice& v, core::FxHitType type, float level, double beat) noexcept {
  v = Voice{};
  v.used = true;
  v.type = type;
  v.beat = beat;
  v.length = hitLengthSamples(type, beat, sampleRate_);
  v.serial = nextSerial_++;
  v.rng = {0x9E3779B9U ^ static_cast<std::uint32_t>(v.serial * 2654435761U),
           0x85EBCA6BU + static_cast<std::uint32_t>(v.serial * 40503U)};
  if (v.rng[0] == 0) v.rng[0] = 1;
  if (v.rng[1] == 0) v.rng[1] = 1;
  for (auto& f : v.svf) f.prepare(sampleRate_);

  float norm = 1.0F;
  switch (type) {
    case core::FxHitType::AirHorn: norm = kNormAirHorn; break;
    case core::FxHitType::Siren: norm = kNormSiren; break;
    case core::FxHitType::Riser: norm = kNormRiser; break;
    case core::FxHitType::Downlifter: norm = kNormDownlifter; break;
    case core::FxHitType::Impact: norm = kNormImpact; break;
    case core::FxHitType::Laser: norm = kNormLaser; break;
  }
  v.gain = level * norm;
  // Saw oscillators start at their zero crossing (phase 0.5), so even the first sample is silent.
  if (type == core::FxHitType::AirHorn) v.phase.fill(0.5);
}

// RT
void FxHitPlayer::process(float* left, float* right, int numSamples) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0) return;
  for (auto& v : voices_) {
    if (v.used) renderVoice(v, left, right, numSamples);
  }
}

void FxHitPlayer::renderVoice(Voice& v, float* left, float* right, int numSamples) noexcept {
  for (int i = 0; i < numSamples; ++i) {
    float l = 0.0F;
    float r = 0.0F;
    sample(v, l, r);
    ++v.n;
    if (!std::isfinite(l) || !std::isfinite(r)) {
      l = 0.0F;
      r = 0.0F;
    }
    float g = v.gain;
    if (v.dying) {
      v.stealGain -= v.stealStep;
      if (v.stealGain <= 0.0F) {
        v.used = false;
        return;
      }
      g *= v.stealGain;
    }
    left[i] += l * g;
    right[i] += r * g;
    if (v.n >= v.length) {
      v.used = false;
      return;
    }
  }
}

void FxHitPlayer::sample(Voice& v, float& l, float& r) noexcept {
  switch (v.type) {
    case core::FxHitType::AirHorn: airHorn(v, l, r); break;
    case core::FxHitType::Siren: siren(v, l, r); break;
    case core::FxHitType::Riser: sweepNoise(v, true, l, r); break;
    case core::FxHitType::Downlifter: sweepNoise(v, false, l, r); break;
    case core::FxHitType::Impact: impact(v, l, r); break;
    case core::FxHitType::Laser: laser(v, l, r); break;
  }
}

// Air horn: two stacked notes, three detuned saws each, gated into the classic "pa pa pa paaa" on the eighth notes.
// Each stab has a 4 ms raised-cosine attack, a pitch scoop at its start and a 12 ms release.
void FxHitPlayer::airHorn(Voice& v, float& l, float& r) noexcept {
  const double beat = v.beat;
  const std::int64_t onset[4] = {0, samplesOf(0.5 * beat, sampleRate_) , samplesOf(1.0 * beat, sampleRate_),
                                 samplesOf(1.5 * beat, sampleRate_)};
  const std::int64_t shortLen = samplesOf(0.3 * beat, sampleRate_);
  const std::int64_t stabLen[4] = {shortLen, shortLen, shortLen, samplesOf(1.45 * beat, sampleRate_)};
  const std::int64_t attack = samplesOf(0.004, sampleRate_);
  const std::int64_t release = samplesOf(0.012, sampleRate_);

  float env = 0.0F;
  double scoop = 1.0;
  for (int k = 0; k < 4; ++k) {
    const std::int64_t s = v.n - onset[k];
    if (s >= 0 && s < stabLen[k]) {
      env = fadeIn(s, attack) * fadeIn(stabLen[k] - 1 - s, release);
      scoop = 1.0 + 0.03 * std::exp(-static_cast<double>(s) / (0.03 * sampleRate_));
      break;
    }
  }

  float mix = 0.0F;
  std::size_t osc = 0;
  for (const double note : kAirHornNotesHz) {
    for (const double detune : kAirHornDetune) {
      const double inc = note * detune * scoop / sampleRate_;
      mix += sawBlep(v.phase[osc], inc);
      advance(v.phase[osc], inc);
      ++osc;
    }
  }
  mix *= 1.0F / 6.0F;

  // Brass-ish: a 12 dB/oct low-pass around 2.8 kHz tames the saw's top.
  const float a = 1.0F - std::exp(-static_cast<float>(kTwoPi * 2800.0 / sampleRate_));
  v.lp[0] += a * (mix - v.lp[0]);
  v.lp[1] += a * (v.lp[0] - v.lp[1]);
  const float out = v.lp[1] * env;
  l = out;
  r = out;
}

// Dub siren: sine + band-limited square, pitch swept by a smooth LFO (one sweep per beat, 400 -> 1600 Hz).
void FxHitPlayer::siren(Voice& v, float& l, float& r) noexcept {
  const double t = static_cast<double>(v.n) / sampleRate_;
  const double lfo = 0.5 - 0.5 * std::cos(kTwoPi * t / v.beat);
  const double freq = 400.0 * std::pow(4.0, lfo);
  const double inc = freq / sampleRate_;
  const float tone = 0.65F * static_cast<float>(std::sin(kTwoPi * v.phase[0])) + 0.35F * squareBlep(v.phase[1], inc);
  advance(v.phase[0], inc);
  advance(v.phase[1], inc);
  const float a = 1.0F - std::exp(-static_cast<float>(kTwoPi * 5000.0 / sampleRate_));
  v.lp[0] += a * (tone - v.lp[0]);
  const float env = fadeIn(v.n, samplesOf(0.015, sampleRate_)) * fadeOutAtEnd(v.n, v.length, samplesOf(0.15, sampleRate_));
  l = v.lp[0] * env;
  r = l;
}

// Riser / downlifter: band-passed stereo noise whose centre moves 300 Hz <-> 12 kHz (exponential) under a sine sweep,
// with an amplitude curve that builds (riser) or dies away (downlifter).
void FxHitPlayer::sweepNoise(Voice& v, bool rising, float& l, float& r) noexcept {
  const double u = static_cast<double>(v.n) / static_cast<double>(std::max<std::int64_t>(1, v.length - 1));
  const double pos = rising ? u : 1.0 - u;
  if (v.n % kFilterUpdateInterval == 0) {
    const float fc = static_cast<float>(300.0 * std::pow(40.0, std::pow(pos, 1.3)));
    v.svf[0].setParameters(fc, 1.6F);
    v.svf[1].setParameters(fc, 1.6F);
  }
  const float nl = v.svf[0].process(nextNoise(v.rng[0])).bandpass;
  const float nr = v.svf[1].process(nextNoise(v.rng[1])).bandpass;

  const double toneHz = 150.0 * std::pow(16.0, pos);
  const float tone = 0.5F * static_cast<float>(std::sin(kTwoPi * v.phase[0])) +
                     0.25F * static_cast<float>(std::sin(kTwoPi * v.phase[1]));
  advance(v.phase[0], toneHz / sampleRate_);
  advance(v.phase[1], toneHz * 1.5 / sampleRate_);

  float env;
  if (rising) {
    const float uf = static_cast<float>(u);
    env = uf * uf * fadeOutAtEnd(v.n, v.length, samplesOf(0.025, sampleRate_));
  } else {
    const float inv = static_cast<float>(1.0 - u);
    env = inv * std::sqrt(std::max(0.0F, inv)) * fadeIn(v.n, samplesOf(0.008, sampleRate_));
  }
  l = (0.35F * nl + tone) * env;
  r = (0.35F * nr + tone) * env;
}

// Impact: a saturated sub-boom whose pitch falls 80 -> 35 Hz plus a low-passed noise crack, both decaying.
void FxHitPlayer::impact(Voice& v, float& l, float& r) noexcept {
  const double t = static_cast<double>(v.n) / sampleRate_;
  const double total = static_cast<double>(v.length) / sampleRate_;
  const double tau = total / 7.0;
  const double freq = 35.0 + 45.0 * std::exp(-t / 0.08);
  const float sub = std::tanh(1.6F * static_cast<float>(std::sin(kTwoPi * v.phase[0])));
  advance(v.phase[0], freq / sampleRate_);

  if (v.n % kFilterUpdateInterval == 0) {
    const float fc = static_cast<float>(200.0 + 3000.0 * std::exp(-t / 0.1));
    v.svf[0].setParameters(fc, 0.8F);
    v.svf[1].setParameters(fc, 0.8F);
  }
  const float nl = v.svf[0].process(nextNoise(v.rng[0])).lowpass;
  const float nr = v.svf[1].process(nextNoise(v.rng[1])).lowpass;

  const float common = fadeIn(v.n, samplesOf(0.003, sampleRate_)) * fadeOutAtEnd(v.n, v.length, samplesOf(0.06, sampleRate_));
  const float subEnv = static_cast<float>(std::exp(-t / tau));
  const float noiseEnv = static_cast<float>(std::exp(-t / (tau * 0.5)));
  l = (0.7F * sub * subEnv + 0.5F * nl * noiseEnv) * common;
  r = (0.7F * sub * subEnv + 0.5F * nr * noiseEnv) * common;
}

// Laser: a fast pitch-down zap, 4.5 kHz -> 200 Hz, sine plus a touch of saw, exponentially decaying.
void FxHitPlayer::laser(Voice& v, float& l, float& r) noexcept {
  const double t = static_cast<double>(v.n) / sampleRate_;
  const double total = static_cast<double>(v.length) / sampleRate_;
  const double freq = 200.0 + 4300.0 * std::exp(-t / (total * 0.12));
  const double inc = freq / sampleRate_;
  const float tone = 0.8F * static_cast<float>(std::sin(kTwoPi * v.phase[0])) + 0.2F * sawBlep(v.phase[1], inc);
  advance(v.phase[0], inc);
  advance(v.phase[1], inc);
  const float env = fadeIn(v.n, samplesOf(0.0015, sampleRate_)) * static_cast<float>(std::exp(-t / (total * 0.25))) *
                    fadeOutAtEnd(v.n, v.length, samplesOf(0.02, sampleRate_));
  l = tone * env;
  r = l;
}

}  // namespace zyron::audio
