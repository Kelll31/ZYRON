// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Effects/DelayEffect.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

const std::array<ParameterDescriptor, DelayEffect::kNumParams> DelayEffect::kDescriptors = {{
    {"time", "Time", 10.0F, 2000.0F, 375.0F, "ms"},
    {"feedback", "Feedback", 0.0F, 0.95F, 0.4F, "%"},
    {"damping", "Damping", 1000.0F, 20000.0F, 8000.0F, "Hz"},
    {"pingpong", "Ping Pong", 0.0F, 1.0F, 0.0F, ""},
}};

DelayEffect::DelayEffect() {
  prepare(48000.0, 1024);
}

void DelayEffect::prepare(double sampleRate, [[maybe_unused]] int maxBlockSize) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

  // Power of 2 buffer capacity: 524288 samples (~10.9s at 48kHz, ~2.73s at 192kHz)
  const std::size_t capacity = 524288;
  bufferL_.assign(capacity, 0.0F);
  bufferR_.assign(capacity, 0.0F);
  bufferMask_ = static_cast<int>(capacity - 1);
  writeIndex_ = 0;

  dampStateL_ = 0.0F;
  dampStateR_ = 0.0F;

  smoothCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.020F));

  const float targetTime = targetTimeMs_.load(std::memory_order_relaxed);
  currentDelaySamples_ = (targetTime * 0.001F) * static_cast<float>(sampleRate_);
  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentFeedback_ = targetFeedback_.load(std::memory_order_relaxed);

  const float dampingHz = targetDampingHz_.load(std::memory_order_relaxed);
  currentDampCoeff_ = std::clamp(2.0F * 3.14159265F * dampingHz / static_cast<float>(sampleRate_), 0.01F, 0.99F);
}

void DelayEffect::reset() noexcept {
  std::fill(bufferL_.begin(), bufferL_.end(), 0.0F);
  std::fill(bufferR_.begin(), bufferR_.end(), 0.0F);
  writeIndex_ = 0;
  dampStateL_ = 0.0F;
  dampStateR_ = 0.0F;

  const float targetTime = targetTimeMs_.load(std::memory_order_relaxed);
  currentDelaySamples_ = (targetTime * 0.001F) * static_cast<float>(sampleRate_);
  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentFeedback_ = targetFeedback_.load(std::memory_order_relaxed);
  const float dampingHz = targetDampingHz_.load(std::memory_order_relaxed);
  currentDampCoeff_ = std::clamp(2.0F * 3.14159265F * dampingHz / static_cast<float>(sampleRate_), 0.01F, 0.99F);
}

void DelayEffect::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

bool DelayEffect::isEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

void DelayEffect::setDryWet(float mixLinear) noexcept {
  targetDryWet_.store(std::clamp(mixLinear, 0.0F, 1.0F), std::memory_order_relaxed);
}

float DelayEffect::dryWet() const noexcept {
  return targetDryWet_.load(std::memory_order_relaxed);
}

std::span<const ParameterDescriptor> DelayEffect::parameters() const noexcept {
  return kDescriptors;
}

void DelayEffect::setParameter(std::size_t index, float value) noexcept {
  switch (index) {
    case 0:
      setDelayTimeMs(value);
      break;
    case 1:
      setFeedback(value);
      break;
    case 2:
      setDampingHz(value);
      break;
    case 3:
      setPingPong(value >= 0.5F);
      break;
    default:
      break;
  }
}

float DelayEffect::parameter(std::size_t index) const noexcept {
  switch (index) {
    case 0:
      return delayTimeMs();
    case 1:
      return feedback();
    case 2:
      return dampingHz();
    case 3:
      return isPingPong() ? 1.0F : 0.0F;
    default:
      return 0.0F;
  }
}

void DelayEffect::setDelayTimeMs(float ms) noexcept {
  targetTimeMs_.store(std::clamp(ms, 10.0F, 2000.0F), std::memory_order_relaxed);
}

float DelayEffect::delayTimeMs() const noexcept {
  return targetTimeMs_.load(std::memory_order_relaxed);
}

void DelayEffect::setFeedback(float feedback) noexcept {
  targetFeedback_.store(std::clamp(feedback, 0.0F, 0.95F), std::memory_order_relaxed);
}

float DelayEffect::feedback() const noexcept {
  return targetFeedback_.load(std::memory_order_relaxed);
}

void DelayEffect::setDampingHz(float cutoffHz) noexcept {
  targetDampingHz_.store(std::clamp(cutoffHz, 1000.0F, 20000.0F), std::memory_order_relaxed);
}

float DelayEffect::dampingHz() const noexcept {
  return targetDampingHz_.load(std::memory_order_relaxed);
}

void DelayEffect::setPingPong(bool enabled) noexcept {
  pingPong_.store(enabled, std::memory_order_relaxed);
}

bool DelayEffect::isPingPong() const noexcept {
  return pingPong_.load(std::memory_order_relaxed);
}

void DelayEffect::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  const bool isEn = enabled_.load(std::memory_order_relaxed);
  if (!isEn) {
    return;
  }

  const float targetTime = targetTimeMs_.load(std::memory_order_relaxed);
  const float targetSamples =
      std::clamp((targetTime * 0.001F) * static_cast<float>(sampleRate_), 1.0F, static_cast<float>(bufferMask_ - 4));
  const float targetFb = targetFeedback_.load(std::memory_order_relaxed);
  const float targetDw = targetDryWet_.load(std::memory_order_relaxed);
  const float targetDampHz = targetDampingHz_.load(std::memory_order_relaxed);
  const float targetDamp =
      std::clamp(2.0F * 3.14159265F * targetDampHz / static_cast<float>(sampleRate_), 0.01F, 0.99F);
  const bool isPP = pingPong_.load(std::memory_order_relaxed);

  float* outL = channels[0];
  float* outR = (numChannels > 1 && channels[1] != nullptr) ? channels[1] : nullptr;

  for (int i = 0; i < numSamples; ++i) {
    // Parameter smoothing
    currentDelaySamples_ += smoothCoeff_ * (targetSamples - currentDelaySamples_);
    currentFeedback_ += smoothCoeff_ * (targetFb - currentFeedback_);
    currentDryWet_ += smoothCoeff_ * (targetDw - currentDryWet_);
    currentDampCoeff_ += smoothCoeff_ * (targetDamp - currentDampCoeff_);

    const float dryL = outL[i];
    const float dryR = (outR != nullptr) ? outR[i] : dryL;

    // Fractional delay read position
    const float readPos = static_cast<float>(writeIndex_) - currentDelaySamples_;
    const float posWrapped = readPos < 0.0F ? (readPos + static_cast<float>(bufferMask_ + 1)) : readPos;
    const int idx0 = static_cast<int>(posWrapped) & bufferMask_;
    const int idx1 = (idx0 + 1) & bufferMask_;
    const float frac = posWrapped - std::floor(posWrapped);

    // Linear interpolation of delayed output
    const float wetL = bufferL_[idx0] + frac * (bufferL_[idx1] - bufferL_[idx0]);
    const float wetR = bufferR_[idx0] + frac * (bufferR_[idx1] - bufferR_[idx0]);

    // Damping one-pole lowpass filter
    dampStateL_ += currentDampCoeff_ * (wetL - dampStateL_);
    dampStateR_ += currentDampCoeff_ * (wetR - dampStateR_);

    // Feedback calculation with soft saturation to avoid runaway
    float fbInL = isPP ? (dryL + dampStateR_ * currentFeedback_) : (dryL + dampStateL_ * currentFeedback_);
    float fbInR = isPP ? (dryR + dampStateL_ * currentFeedback_) : (dryR + dampStateR_ * currentFeedback_);

    // Soft clip: tanh-like rational approximation
    fbInL = fbInL / (1.0F + 0.3F * std::abs(fbInL));
    fbInR = fbInR / (1.0F + 0.3F * std::abs(fbInR));

    // Write into delay lines
    bufferL_[writeIndex_] = fbInL;
    bufferR_[writeIndex_] = fbInR;

    writeIndex_ = (writeIndex_ + 1) & bufferMask_;

    // Mix dry and wet
    const float dw = currentDryWet_;
    outL[i] = (1.0F - dw) * dryL + dw * wetL;
    if (outR != nullptr) {
      outR[i] = (1.0F - dw) * dryR + dw * wetR;
    }
  }
}

}  // namespace zyron::audio
