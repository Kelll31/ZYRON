// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Effects/EchoEffect.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace zyron::audio {

const std::array<ParameterDescriptor, EchoEffect::kNumParams> EchoEffect::kDescriptors = {{
    {"time", "Time", 20.0F, 1500.0F, 250.0F, "ms"},
    {"feedback", "Feedback", 0.0F, 0.95F, 0.5F, "%"},
    {"hp_cutoff", "HP Cutoff", 20.0F, 1000.0F, 200.0F, "Hz"},
    {"lp_cutoff", "LP Cutoff", 1000.0F, 16000.0F, 6000.0F, "Hz"},
    {"freeze", "Freeze", 0.0F, 1.0F, 0.0F, ""},
}};

EchoEffect::EchoEffect() {
  prepare(48000.0, 1024);
}

void EchoEffect::prepare(double sampleRate, [[maybe_unused]] int maxBlockSize) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

  // Power of 2 that holds the longest echo (1.5 s) at this rate: 131072 frames at 48 kHz (1 MB for both channels).
  const std::size_t capacity = std::bit_ceil(static_cast<std::size_t>(sampleRate_ * 1.55) + 8);
  bufferL_.assign(capacity, 0.0F);
  bufferR_.assign(capacity, 0.0F);
  bufferMask_ = static_cast<int>(capacity - 1);
  writeIndex_ = 0;

  hpStateL_ = 0.0F;
  hpStateR_ = 0.0F;
  lpStateL_ = 0.0F;
  lpStateR_ = 0.0F;

  smoothCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.020F));

  const float targetTime = targetTimeMs_.load(std::memory_order_relaxed);
  currentDelaySamples_ = (targetTime * 0.001F) * static_cast<float>(sampleRate_);
  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentFeedback_ = targetFeedback_.load(std::memory_order_relaxed);

  const float hpHz = targetHpHz_.load(std::memory_order_relaxed);
  currentHpCoeff_ = std::clamp(2.0F * 3.14159265F * hpHz / static_cast<float>(sampleRate_), 0.001F, 0.5F);

  const float lpHz = targetLpHz_.load(std::memory_order_relaxed);
  currentLpCoeff_ = std::clamp(2.0F * 3.14159265F * lpHz / static_cast<float>(sampleRate_), 0.05F, 0.95F);
}

void EchoEffect::reset() noexcept {
  std::fill(bufferL_.begin(), bufferL_.end(), 0.0F);
  std::fill(bufferR_.begin(), bufferR_.end(), 0.0F);
  writeIndex_ = 0;
  hpStateL_ = 0.0F;
  hpStateR_ = 0.0F;
  lpStateL_ = 0.0F;
  lpStateR_ = 0.0F;

  const float targetTime = targetTimeMs_.load(std::memory_order_relaxed);
  currentDelaySamples_ = (targetTime * 0.001F) * static_cast<float>(sampleRate_);
  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentFeedback_ = targetFeedback_.load(std::memory_order_relaxed);
  const float hpHz = targetHpHz_.load(std::memory_order_relaxed);
  currentHpCoeff_ = std::clamp(2.0F * 3.14159265F * hpHz / static_cast<float>(sampleRate_), 0.001F, 0.5F);
  const float lpHz = targetLpHz_.load(std::memory_order_relaxed);
  currentLpCoeff_ = std::clamp(2.0F * 3.14159265F * lpHz / static_cast<float>(sampleRate_), 0.05F, 0.95F);
}

void EchoEffect::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

bool EchoEffect::isEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

void EchoEffect::setDryWet(float mixLinear) noexcept {
  targetDryWet_.store(std::clamp(mixLinear, 0.0F, 1.0F), std::memory_order_relaxed);
}

float EchoEffect::dryWet() const noexcept {
  return targetDryWet_.load(std::memory_order_relaxed);
}

std::span<const ParameterDescriptor> EchoEffect::parameters() const noexcept {
  return kDescriptors;
}

void EchoEffect::setParameter(std::size_t index, float value) noexcept {
  switch (index) {
    case 0:
      setEchoTimeMs(value);
      break;
    case 1:
      setFeedback(value);
      break;
    case 2:
      setHighpassHz(value);
      break;
    case 3:
      setLowpassHz(value);
      break;
    case 4:
      setFreeze(value >= 0.5F);
      break;
    default:
      break;
  }
}

float EchoEffect::parameter(std::size_t index) const noexcept {
  switch (index) {
    case 0:
      return echoTimeMs();
    case 1:
      return feedback();
    case 2:
      return highpassHz();
    case 3:
      return lowpassHz();
    case 4:
      return isFrozen() ? 1.0F : 0.0F;
    default:
      return 0.0F;
  }
}

void EchoEffect::setEchoTimeMs(float ms) noexcept {
  targetTimeMs_.store(std::clamp(ms, 20.0F, 1500.0F), std::memory_order_relaxed);
}

float EchoEffect::echoTimeMs() const noexcept {
  return targetTimeMs_.load(std::memory_order_relaxed);
}

void EchoEffect::setFeedback(float feedback) noexcept {
  targetFeedback_.store(std::clamp(feedback, 0.0F, 0.95F), std::memory_order_relaxed);
}

float EchoEffect::feedback() const noexcept {
  return targetFeedback_.load(std::memory_order_relaxed);
}

void EchoEffect::setHighpassHz(float hpHz) noexcept {
  targetHpHz_.store(std::clamp(hpHz, 20.0F, 1000.0F), std::memory_order_relaxed);
}

float EchoEffect::highpassHz() const noexcept {
  return targetHpHz_.load(std::memory_order_relaxed);
}

void EchoEffect::setLowpassHz(float lpHz) noexcept {
  targetLpHz_.store(std::clamp(lpHz, 1000.0F, 16000.0F), std::memory_order_relaxed);
}

float EchoEffect::lowpassHz() const noexcept {
  return targetLpHz_.load(std::memory_order_relaxed);
}

void EchoEffect::setFreeze(bool frozen) noexcept {
  frozen_.store(frozen, std::memory_order_relaxed);
}

bool EchoEffect::isFrozen() const noexcept {
  return frozen_.load(std::memory_order_relaxed);
}

void EchoEffect::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }

  const float targetTime = targetTimeMs_.load(std::memory_order_relaxed);
  const float targetSamples =
      std::clamp((targetTime * 0.001F) * static_cast<float>(sampleRate_), 1.0F, static_cast<float>(bufferMask_ - 4));
  const bool isFrozenLoop = frozen_.load(std::memory_order_relaxed);
  const float targetFb = isFrozenLoop ? 1.0F : targetFeedback_.load(std::memory_order_relaxed);
  const float targetDw = targetDryWet_.load(std::memory_order_relaxed);

  const float targetHp = std::clamp(
      2.0F * 3.14159265F * targetHpHz_.load(std::memory_order_relaxed) / static_cast<float>(sampleRate_), 0.001F, 0.5F);
  const float targetLp = std::clamp(
      2.0F * 3.14159265F * targetLpHz_.load(std::memory_order_relaxed) / static_cast<float>(sampleRate_), 0.05F, 0.95F);

  float* outL = channels[0];
  float* outR = (numChannels > 1 && channels[1] != nullptr) ? channels[1] : nullptr;

  for (int i = 0; i < numSamples; ++i) {
    currentDelaySamples_ += smoothCoeff_ * (targetSamples - currentDelaySamples_);
    currentFeedback_ += smoothCoeff_ * (targetFb - currentFeedback_);
    currentDryWet_ += smoothCoeff_ * (targetDw - currentDryWet_);
    currentHpCoeff_ += smoothCoeff_ * (targetHp - currentHpCoeff_);
    currentLpCoeff_ += smoothCoeff_ * (targetLp - currentLpCoeff_);

    const float dryL = outL[i];
    const float dryR = (outR != nullptr) ? outR[i] : dryL;

    // Fractional delay read
    const float readPos = static_cast<float>(writeIndex_) - currentDelaySamples_;
    const float posWrapped = readPos < 0.0F ? (readPos + static_cast<float>(bufferMask_ + 1)) : readPos;
    const int idx0 = static_cast<int>(posWrapped) & bufferMask_;
    const int idx1 = (idx0 + 1) & bufferMask_;
    const float frac = posWrapped - std::floor(posWrapped);

    const float wetL = bufferL_[idx0] + frac * (bufferL_[idx1] - bufferL_[idx0]);
    const float wetR = bufferR_[idx0] + frac * (bufferR_[idx1] - bufferR_[idx0]);

    // Bandpass in feedback loop: Lowpass -> Highpass
    // 1-pole Lowpass
    lpStateL_ += currentLpCoeff_ * (wetL - lpStateL_);
    lpStateR_ += currentLpCoeff_ * (wetR - lpStateR_);

    // 1-pole Highpass: y = x - lp(x)
    hpStateL_ += currentHpCoeff_ * (lpStateL_ - hpStateL_);
    hpStateR_ += currentHpCoeff_ * (lpStateR_ - hpStateR_);
    const float filteredL = lpStateL_ - hpStateL_;
    const float filteredR = lpStateR_ - hpStateR_;

    // If frozen, don't inject input signal; just recycle echo buffer
    const float inputGain = isFrozenLoop ? 0.0F : 1.0F;
    float fbInL = inputGain * dryL + filteredL * currentFeedback_;
    float fbInR = inputGain * dryR + filteredR * currentFeedback_;

    // Tape saturation (tanh-like rational saturation)
    fbInL = fbInL / (1.0F + 0.25F * std::abs(fbInL));
    fbInR = fbInR / (1.0F + 0.25F * std::abs(fbInR));

    bufferL_[writeIndex_] = fbInL;
    bufferR_[writeIndex_] = fbInR;

    writeIndex_ = (writeIndex_ + 1) & bufferMask_;

    const float dw = currentDryWet_;
    outL[i] = (1.0F - dw) * dryL + dw * wetL;
    if (outR != nullptr) {
      outR[i] = (1.0F - dw) * dryR + dw * wetR;
    }
  }
}

}  // namespace zyron::audio
