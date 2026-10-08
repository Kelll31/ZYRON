// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Effects/PhaserEffect.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

const std::array<ParameterDescriptor, PhaserEffect::kNumParams> PhaserEffect::kDescriptors = {{
    {"rate", "Rate", 0.05F, 5.0F, 0.3F, "Hz"},
    {"depth", "Depth", 0.0F, 1.0F, 0.8F, "%"},
    {"feedback", "Feedback", 0.0F, 0.9F, 0.5F, "%"},
    {"frequency", "Base Freq", 200.0F, 4000.0F, 1000.0F, "Hz"},
}};

PhaserEffect::PhaserEffect() {
  prepare(48000.0, 1024);
}

void PhaserEffect::prepare(double sampleRate, [[maybe_unused]] int maxBlockSize) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  smoothCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.020F));

  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentDepth_ = targetDepth_.load(std::memory_order_relaxed);
  currentFeedback_ = targetFeedback_.load(std::memory_order_relaxed);
  currentBaseFreqHz_ = targetBaseFreqHz_.load(std::memory_order_relaxed);
  reset();
}

void PhaserEffect::reset() noexcept {
  stagesL_.fill(0.0F);
  stagesR_.fill(0.0F);
  lastOutputL_ = 0.0F;
  lastOutputR_ = 0.0F;
  lfoPhase_ = 0.0F;

  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentDepth_ = targetDepth_.load(std::memory_order_relaxed);
  currentFeedback_ = targetFeedback_.load(std::memory_order_relaxed);
  currentBaseFreqHz_ = targetBaseFreqHz_.load(std::memory_order_relaxed);
}

void PhaserEffect::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

bool PhaserEffect::isEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

void PhaserEffect::setDryWet(float mixLinear) noexcept {
  targetDryWet_.store(std::clamp(mixLinear, 0.0F, 1.0F), std::memory_order_relaxed);
}

float PhaserEffect::dryWet() const noexcept {
  return targetDryWet_.load(std::memory_order_relaxed);
}

std::span<const ParameterDescriptor> PhaserEffect::parameters() const noexcept {
  return kDescriptors;
}

void PhaserEffect::setParameter(std::size_t index, float value) noexcept {
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
      setBaseFrequencyHz(value);
      break;
    default:
      break;
  }
}

float PhaserEffect::parameter(std::size_t index) const noexcept {
  switch (index) {
    case 0:
      return rateHz();
    case 1:
      return depth();
    case 2:
      return feedback();
    case 3:
      return baseFrequencyHz();
    default:
      return 0.0F;
  }
}

void PhaserEffect::setRateHz(float rate) noexcept {
  targetRateHz_.store(std::clamp(rate, 0.05F, 5.0F), std::memory_order_relaxed);
}

float PhaserEffect::rateHz() const noexcept {
  return targetRateHz_.load(std::memory_order_relaxed);
}

void PhaserEffect::setDepth(float depth) noexcept {
  targetDepth_.store(std::clamp(depth, 0.0F, 1.0F), std::memory_order_relaxed);
}

float PhaserEffect::depth() const noexcept {
  return targetDepth_.load(std::memory_order_relaxed);
}

void PhaserEffect::setFeedback(float feedback) noexcept {
  targetFeedback_.store(std::clamp(feedback, 0.0F, 0.9F), std::memory_order_relaxed);
}

float PhaserEffect::feedback() const noexcept {
  return targetFeedback_.load(std::memory_order_relaxed);
}

void PhaserEffect::setBaseFrequencyHz(float freqHz) noexcept {
  targetBaseFreqHz_.store(std::clamp(freqHz, 200.0F, 4000.0F), std::memory_order_relaxed);
}

float PhaserEffect::baseFrequencyHz() const noexcept {
  return targetBaseFreqHz_.load(std::memory_order_relaxed);
}

void PhaserEffect::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }

  const float rateHz = targetRateHz_.load(std::memory_order_relaxed);
  const float lfoPhaseInc = (rateHz / static_cast<float>(sampleRate_));

  const float targetDp = targetDepth_.load(std::memory_order_relaxed);
  const float targetFb = targetFeedback_.load(std::memory_order_relaxed);
  const float targetF0 = targetBaseFreqHz_.load(std::memory_order_relaxed);
  const float targetDw = targetDryWet_.load(std::memory_order_relaxed);

  float* outL = channels[0];
  float* outR = (numChannels > 1 && channels[1] != nullptr) ? channels[1] : nullptr;

  constexpr float kPi = 3.1415926535F;
  constexpr float kTwoPi = 6.283185307F;

  for (int i = 0; i < numSamples; ++i) {
    currentDepth_ += smoothCoeff_ * (targetDp - currentDepth_);
    currentFeedback_ += smoothCoeff_ * (targetFb - currentFeedback_);
    currentBaseFreqHz_ += smoothCoeff_ * (targetF0 - currentBaseFreqHz_);
    currentDryWet_ += smoothCoeff_ * (targetDw - currentDryWet_);

    // Quadrature LFO for stereo spread (90 degrees phase offset)
    const float lfoL = std::sin(kTwoPi * lfoPhase_);
    const float lfoR = std::sin(kTwoPi * (lfoPhase_ + 0.25F));

    lfoPhase_ += lfoPhaseInc;
    if (lfoPhase_ >= 1.0F) {
      lfoPhase_ -= 1.0F;
    }

    // Modulate cutoff frequency over ~2 octaves
    const float fcL = std::clamp(currentBaseFreqHz_ * std::pow(2.0F, lfoL * currentDepth_ * 1.5F), 50.0F,
                                 0.45F * static_cast<float>(sampleRate_));
    const float fcR = std::clamp(currentBaseFreqHz_ * std::pow(2.0F, lfoR * currentDepth_ * 1.5F), 50.0F,
                                 0.45F * static_cast<float>(sampleRate_));

    const float wL = std::tan(kPi * fcL / static_cast<float>(sampleRate_));
    const float aL = (1.0F - wL) / (1.0F + wL);

    const float wR = std::tan(kPi * fcR / static_cast<float>(sampleRate_));
    const float aR = (1.0F - wR) / (1.0F + wR);

    const float dryL = outL[i];
    const float dryR = (outR != nullptr) ? outR[i] : dryL;

    // Input with feedback (soft clipped)
    float inL = dryL - currentFeedback_ * lastOutputL_;
    float inR = dryR - currentFeedback_ * lastOutputR_;
    inL = inL / (1.0F + 0.2F * std::abs(inL));
    inR = inR / (1.0F + 0.2F * std::abs(inR));

    // 6-stage allpass cascade Left
    float sigL = inL;
    for (std::size_t s = 0; s < kNumStages; ++s) {
      const float y = aL * sigL + stagesL_[s];
      stagesL_[s] = sigL - aL * y;
      sigL = y;
    }
    lastOutputL_ = sigL;

    // 6-stage allpass cascade Right
    float sigR = inR;
    for (std::size_t s = 0; s < kNumStages; ++s) {
      const float y = aR * sigR + stagesR_[s];
      stagesR_[s] = sigR - aR * y;
      sigR = y;
    }
    lastOutputR_ = sigR;

    const float dw = currentDryWet_;
    outL[i] = (1.0F - dw) * dryL + dw * sigL;
    if (outR != nullptr) {
      outR[i] = (1.0F - dw) * dryR + dw * sigR;
    }
  }
}

}  // namespace zyron::audio
