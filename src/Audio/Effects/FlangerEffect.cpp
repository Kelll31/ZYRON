// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Effects/FlangerEffect.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

const std::array<ParameterDescriptor, FlangerEffect::kNumParams> FlangerEffect::kDescriptors = {{
    {"rate", "Rate", 0.05F, 5.0F, 0.25F, "Hz"},
    {"depth", "Depth", 0.0F, 1.0F, 0.7F, "%"},
    {"feedback", "Feedback", -0.95F, 0.95F, 0.5F, "%"},
    {"stereo_phase", "Stereo Phase", 0.0F, 180.0F, 90.0F, "deg"},
}};

FlangerEffect::FlangerEffect() {
  prepare(48000.0, 1024);
}

void FlangerEffect::prepare(double sampleRate, [[maybe_unused]] int maxBlockSize) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

  // 8192 samples is plenty for up to ~40 ms delay at 192 kHz
  const std::size_t capacity = 8192;
  bufferL_.assign(capacity, 0.0F);
  bufferR_.assign(capacity, 0.0F);
  bufferMask_ = static_cast<int>(capacity - 1);
  writeIndex_ = 0;
  lfoPhase_ = 0.0F;

  smoothCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.020F));

  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentDepth_ = targetDepth_.load(std::memory_order_relaxed);
  currentFeedback_ = targetFeedback_.load(std::memory_order_relaxed);
}

void FlangerEffect::reset() noexcept {
  std::fill(bufferL_.begin(), bufferL_.end(), 0.0F);
  std::fill(bufferR_.begin(), bufferR_.end(), 0.0F);
  writeIndex_ = 0;
  lfoPhase_ = 0.0F;

  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentDepth_ = targetDepth_.load(std::memory_order_relaxed);
  currentFeedback_ = targetFeedback_.load(std::memory_order_relaxed);
}

void FlangerEffect::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

bool FlangerEffect::isEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

void FlangerEffect::setDryWet(float mixLinear) noexcept {
  targetDryWet_.store(std::clamp(mixLinear, 0.0F, 1.0F), std::memory_order_relaxed);
}

float FlangerEffect::dryWet() const noexcept {
  return targetDryWet_.load(std::memory_order_relaxed);
}

std::span<const ParameterDescriptor> FlangerEffect::parameters() const noexcept {
  return kDescriptors;
}

void FlangerEffect::setParameter(std::size_t index, float value) noexcept {
  switch (index) {
    case 0:
      setRateHz(value);
      break;
    case 1:
      setDepth(value);
      break;
    case 2:
      setFeedback(value);
      break;
    case 3:
      setStereoPhaseDeg(value);
      break;
    default:
      break;
  }
}

float FlangerEffect::parameter(std::size_t index) const noexcept {
  switch (index) {
    case 0:
      return rateHz();
    case 1:
      return depth();
    case 2:
      return feedback();
    case 3:
      return stereoPhaseDeg();
    default:
      return 0.0F;
  }
}

void FlangerEffect::setRateHz(float rate) noexcept {
  targetRateHz_.store(std::clamp(rate, 0.05F, 5.0F), std::memory_order_relaxed);
}

float FlangerEffect::rateHz() const noexcept {
  return targetRateHz_.load(std::memory_order_relaxed);
}

void FlangerEffect::setDepth(float depth) noexcept {
  targetDepth_.store(std::clamp(depth, 0.0F, 1.0F), std::memory_order_relaxed);
}

float FlangerEffect::depth() const noexcept {
  return targetDepth_.load(std::memory_order_relaxed);
}

void FlangerEffect::setFeedback(float feedback) noexcept {
  targetFeedback_.store(std::clamp(feedback, -0.95F, 0.95F), std::memory_order_relaxed);
}

float FlangerEffect::feedback() const noexcept {
  return targetFeedback_.load(std::memory_order_relaxed);
}

void FlangerEffect::setStereoPhaseDeg(float phaseDeg) noexcept {
  targetStereoPhaseDeg_.store(std::clamp(phaseDeg, 0.0F, 180.0F), std::memory_order_relaxed);
}

float FlangerEffect::stereoPhaseDeg() const noexcept {
  return targetStereoPhaseDeg_.load(std::memory_order_relaxed);
}

void FlangerEffect::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }

  const float rateHz = targetRateHz_.load(std::memory_order_relaxed);
  const float lfoPhaseInc = (rateHz / static_cast<float>(sampleRate_));
  const float phaseOffset = (targetStereoPhaseDeg_.load(std::memory_order_relaxed) / 360.0F);

  const float targetDp = targetDepth_.load(std::memory_order_relaxed);
  const float targetFb = targetFeedback_.load(std::memory_order_relaxed);
  const float targetDw = targetDryWet_.load(std::memory_order_relaxed);

  float* outL = channels[0];
  float* outR = (numChannels > 1 && channels[1] != nullptr) ? channels[1] : nullptr;

  constexpr float kBaseDelaySec = 0.001F;  // 1.0 ms base delay
  constexpr float kMaxModSec = 0.005F;    // 5.0 ms max modulation depth
  constexpr float kTwoPi = 6.283185307F;

  for (int i = 0; i < numSamples; ++i) {
    currentDepth_ += smoothCoeff_ * (targetDp - currentDepth_);
    currentFeedback_ += smoothCoeff_ * (targetFb - currentFeedback_);
    currentDryWet_ += smoothCoeff_ * (targetDw - currentDryWet_);

    // LFO calculations (sine oscillator)
    const float lfoL = 0.5F * (std::sin(kTwoPi * lfoPhase_) + 1.0F);
    const float lfoR = 0.5F * (std::sin(kTwoPi * (lfoPhase_ + phaseOffset)) + 1.0F);

    lfoPhase_ += lfoPhaseInc;
    if (lfoPhase_ >= 1.0F) {
      lfoPhase_ -= 1.0F;
    }

    const float delaySecL = kBaseDelaySec + currentDepth_ * kMaxModSec * lfoL;
    const float delaySecR = kBaseDelaySec + currentDepth_ * kMaxModSec * lfoR;

    const float delaySamplesL = delaySecL * static_cast<float>(sampleRate_);
    const float delaySamplesR = delaySecR * static_cast<float>(sampleRate_);

    const float dryL = outL[i];
    const float dryR = (outR != nullptr) ? outR[i] : dryL;

    // Read Left
    const float readPosL = static_cast<float>(writeIndex_) - delaySamplesL;
    const float posWrapL = readPosL < 0.0F ? (readPosL + static_cast<float>(bufferMask_ + 1)) : readPosL;
    const int idxL0 = static_cast<int>(posWrapL) & bufferMask_;
    const int idxL1 = (idxL0 + 1) & bufferMask_;
    const float fracL = posWrapL - std::floor(posWrapL);
    const float wetL = bufferL_[idxL0] + fracL * (bufferL_[idxL1] - bufferL_[idxL0]);

    // Read Right
    const float readPosR = static_cast<float>(writeIndex_) - delaySamplesR;
    const float posWrapR = readPosR < 0.0F ? (readPosR + static_cast<float>(bufferMask_ + 1)) : readPosR;
    const int idxR0 = static_cast<int>(posWrapR) & bufferMask_;
    const int idxR1 = (idxR0 + 1) & bufferMask_;
    const float fracR = posWrapR - std::floor(posWrapR);
    const float wetR = bufferR_[idxR0] + fracR * (bufferR_[idxR1] - bufferR_[idxR0]);

    // Feedback with soft saturation
    float fbInL = dryL + currentFeedback_ * wetL;
    float fbInR = dryR + currentFeedback_ * wetR;
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
