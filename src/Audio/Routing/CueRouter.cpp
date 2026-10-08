// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Routing/CueRouter.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

CueRouter::CueRouter() {
  prepare(48000.0);
}

void CueRouter::prepare(double sampleRate) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  // 10 ms parameter smoothing
  rampCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.010F));
  reset();
}

void CueRouter::reset() noexcept {
  currentVolume_ = targetVolume_.load(std::memory_order_relaxed);
  currentMix_ = targetMix_.load(std::memory_order_relaxed);
  delayL_.fill(0.0F);
  delayR_.fill(0.0F);
  delayWrite_ = 0;
}

void CueRouter::setCueDelaySamples(int samples) noexcept {
  cueDelay_ = std::clamp(samples, 0, kMaxCueDelay);
}

void CueRouter::setMode(HeadphoneRoutingMode mode) noexcept {
  mode_.store(mode, std::memory_order_relaxed);
}

HeadphoneRoutingMode CueRouter::mode() const noexcept {
  return mode_.load(std::memory_order_relaxed);
}

void CueRouter::setHeadphoneVolume(float volume) noexcept {
  volume = std::clamp(volume, 0.0F, 1.0F);
  targetVolume_.store(volume, std::memory_order_relaxed);
}

float CueRouter::headphoneVolume() const noexcept {
  return targetVolume_.load(std::memory_order_relaxed);
}

void CueRouter::setHeadphoneMix(float mix) noexcept {
  mix = std::clamp(mix, 0.0F, 1.0F);
  targetMix_.store(mix, std::memory_order_relaxed);
}

float CueRouter::headphoneMix() const noexcept {
  return targetMix_.load(std::memory_order_relaxed);
}

void CueRouter::route(const float* masterL, const float* masterR, const float* cueL, const float* cueR,
                      float* const* outputs, int numOutputChannels, int numSamples) noexcept {
  if (outputs == nullptr || numOutputChannels <= 0 || numSamples <= 0) {
    return;
  }

  // Pre-fill all non-null channels with silence
  for (int ch = 0; ch < numOutputChannels; ++ch) {
    if (outputs[ch] != nullptr) {
      std::fill_n(outputs[ch], numSamples, 0.0F);
    }
  }

  const auto activeMode = mode_.load(std::memory_order_relaxed);
  const float targetVol = targetVolume_.load(std::memory_order_relaxed);
  const float targetMx = targetMix_.load(std::memory_order_relaxed);

  for (int i = 0; i < numSamples; ++i) {
    currentVolume_ += rampCoeff_ * (targetVol - currentVolume_);
    currentMix_ += rampCoeff_ * (targetMx - currentMix_);

    const float mL = (masterL != nullptr) ? masterL[i] : 0.0F;
    const float mR = (masterR != nullptr) ? masterR[i] : 0.0F;
    delayL_[delayWrite_] = (cueL != nullptr) ? cueL[i] : 0.0F;
    delayR_[delayWrite_] = (cueR != nullptr) ? cueR[i] : 0.0F;
    const std::size_t readAt = (delayWrite_ + kDelayRing - static_cast<std::size_t>(cueDelay_)) & (kDelayRing - 1);
    delayWrite_ = (delayWrite_ + 1) & (kDelayRing - 1);
    const float cL = delayL_[readAt];
    const float cR = delayR_[readAt];

    if (activeMode == HeadphoneRoutingMode::SplitCue) {
      // Split-cue fallback for 2-channel cards: Left = Cue mono, Right = Master mono
      const float cMono = 0.5F * (cL + cR) * currentVolume_;
      const float mMono = 0.5F * (mL + mR);

      if (outputs[0] != nullptr) {
        outputs[0][i] = cMono;
      }
      if (numOutputChannels > 1 && outputs[1] != nullptr) {
        outputs[1][i] = mMono;
      }
    } else if (activeMode == HeadphoneRoutingMode::MultiChannel && numOutputChannels >= 4) {
      // 4-channel soundcard: Ch 0,1 = Master; Ch 2,3 = Headphones
      if (outputs[0] != nullptr) {
        outputs[0][i] = mL;
      }
      if (outputs[1] != nullptr) {
        outputs[1][i] = mR;
      }

      const float hpL = (cL * (1.0F - currentMix_) + mL * currentMix_) * currentVolume_;
      const float hpR = (cR * (1.0F - currentMix_) + mR * currentMix_) * currentVolume_;

      if (outputs[2] != nullptr) {
        outputs[2][i] = hpL;
      }
      if (outputs[3] != nullptr) {
        outputs[3][i] = hpR;
      }
    } else {
      // Disabled or 2-channel default: Ch 0,1 = Master
      if (outputs[0] != nullptr) {
        outputs[0][i] = mL;
      }
      if (numOutputChannels > 1 && outputs[1] != nullptr) {
        outputs[1][i] = mR;
      }
    }
  }
}

}  // namespace zyron::audio
