// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/TestToneGenerator.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace zyron::audio {
namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
/// Below this the fading-out tone is inaudible (-140 dB); the gain is snapped to exactly 0 so silence is bit-exact
/// and no denormal numbers linger in the smoother.
constexpr double kSilenceThreshold = 1.0e-7;

// The control parameters are packed into one 64-bit word so the audio thread reads them as one consistent snapshot:
//   bit 0       enabled
//   bit 1       suspended
//   bits 2..15  level: (levelDb - kMinLevelDb) in 0.01 dB steps (0..9600 fits in 14 bits)
//   bits 32..63 frequency: the IEEE-754 bits of a float
constexpr std::uint64_t kEnabledBit = 1ULL << 0;
constexpr std::uint64_t kSuspendedBit = 1ULL << 1;
constexpr int kLevelShift = 2;
constexpr std::uint64_t kLevelMask = 0x3FFFULL;
constexpr int kFrequencyShift = 32;
constexpr float kLevelStepDb = 0.01F;

struct Params {
  bool enabled{false};
  bool suspended{false};
  float frequencyHz{TestToneGenerator::kDefaultFrequencyHz};
  float levelDb{TestToneGenerator::kDefaultLevelDb};
};

std::uint64_t pack(const Params& params) noexcept {
  const auto levelCode =
      static_cast<std::uint64_t>(std::lround((params.levelDb - TestToneGenerator::kMinLevelDb) / kLevelStepDb));
  return (params.enabled ? kEnabledBit : 0ULL) | (params.suspended ? kSuspendedBit : 0ULL) |
         ((levelCode & kLevelMask) << kLevelShift) |
         (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(params.frequencyHz)) << kFrequencyShift);
}

Params unpack(std::uint64_t word) noexcept {
  Params params;
  params.enabled = (word & kEnabledBit) != 0;
  params.suspended = (word & kSuspendedBit) != 0;
  params.levelDb =
      TestToneGenerator::kMinLevelDb + static_cast<float>((word >> kLevelShift) & kLevelMask) * kLevelStepDb;
  params.frequencyHz = std::bit_cast<float>(static_cast<std::uint32_t>(word >> kFrequencyShift));
  return params;
}

/// Atomically applies `change` to one field without disturbing the others (compare-and-swap loop).
template <class Change>
void updateParams(std::atomic<std::uint64_t>& word, Change change) noexcept {
  std::uint64_t current = word.load(std::memory_order_relaxed);
  for (;;) {
    Params params = unpack(current);
    change(params);
    if (word.compare_exchange_weak(current, pack(params), std::memory_order_release, std::memory_order_relaxed)) {
      return;
    }
  }
}

float sanitizeFrequency(float hz) noexcept {
  if (!std::isfinite(hz)) {
    return TestToneGenerator::kDefaultFrequencyHz;
  }
  return std::clamp(hz, TestToneGenerator::kMinFrequencyHz, TestToneGenerator::kMaxFrequencyHz);
}

float sanitizeLevel(float db) noexcept {
  if (!std::isfinite(db)) {
    return TestToneGenerator::kDefaultLevelDb;
  }
  return std::clamp(db, TestToneGenerator::kMinLevelDb, TestToneGenerator::kMaxLevelDb);
}

}  // namespace

TestToneGenerator::TestToneGenerator() noexcept : params_(pack(Params{})) {}

void TestToneGenerator::prepare(double sampleRate) noexcept {
  sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 0.0;
  smoothingCoefficient_ = sampleRate_ > 0.0 ? 1.0 - std::exp(-1.0 / (kSmoothingSeconds * sampleRate_)) : 0.0;
  phase_ = 0.0;
  gain_ = 0.0;
  fadedOut_.store(true, std::memory_order_release);
}

void TestToneGenerator::setEnabled(bool enabled) noexcept {
  updateParams(params_, [enabled](Params& p) { p.enabled = enabled; });
}

bool TestToneGenerator::enabled() const noexcept {
  return unpack(params_.load(std::memory_order_acquire)).enabled;
}

void TestToneGenerator::setSuspended(bool suspended) noexcept {
  updateParams(params_, [suspended](Params& p) { p.suspended = suspended; });
}

void TestToneGenerator::setFrequencyHz(float frequencyHz) noexcept {
  updateParams(params_, [f = sanitizeFrequency(frequencyHz)](Params& p) { p.frequencyHz = f; });
}

void TestToneGenerator::setLevelDb(float levelDb) noexcept {
  updateParams(params_, [db = sanitizeLevel(levelDb)](Params& p) { p.levelDb = db; });
}

bool TestToneGenerator::isSilent() const noexcept {
  const Params params = unpack(params_.load(std::memory_order_acquire));
  return gain_ == 0.0 && !(params.enabled && !params.suspended);
}

// RT
void TestToneGenerator::render(float* output, int numSamples) noexcept {
  if (output == nullptr || numSamples <= 0) {
    return;
  }
  if (sampleRate_ <= 0.0) {
    std::fill_n(output, numSamples, 0.0F);
    return;
  }

  // One consistent snapshot of every parameter per block; the smoother below turns any change into a ramp.
  const Params params = unpack(params_.load(std::memory_order_acquire));
  const bool audible = params.enabled && !params.suspended;
  const double target = audible ? std::pow(10.0, static_cast<double>(params.levelDb) / 20.0) : 0.0;
  const double hz = std::min(static_cast<double>(params.frequencyHz), kMaxFractionOfSampleRate * sampleRate_);
  const double increment = kTwoPi * hz / sampleRate_;

  if (target == 0.0 && gain_ == 0.0) {
    // Silent and staying silent: no sine needed, but the phase keeps running so a later switch-on is continuous.
    std::fill_n(output, numSamples, 0.0F);
    phase_ = std::fmod(phase_ + increment * static_cast<double>(numSamples), kTwoPi);
    fadedOut_.store(true, std::memory_order_release);
    return;
  }

  double gain = gain_;
  double phase = phase_;
  for (int i = 0; i < numSamples; ++i) {
    gain += (target - gain) * smoothingCoefficient_;
    if (target == 0.0 && gain < kSilenceThreshold) {
      gain = 0.0;
    }
    output[i] = static_cast<float>(std::sin(phase) * gain);
    phase += increment;
    if (phase >= kTwoPi) {
      phase = std::fmod(phase, kTwoPi);
    }
  }
  gain_ = gain;
  phase_ = phase;
  fadedOut_.store(target == 0.0 && gain < kFadedGain, std::memory_order_release);
}

}  // namespace zyron::audio
