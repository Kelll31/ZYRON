// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/MasterLimiter.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

namespace {

float dbToLinear(float db) noexcept {
  return std::pow(10.0F, db * 0.05F);
}

}  // namespace

MasterLimiter::MasterLimiter() {
  updateCoefficients();
}

void MasterLimiter::prepare(double sampleRate) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  updateCoefficients();
  reset();
}

void MasterLimiter::reset() noexcept {
  delayL_.fill(0.0F);
  delayR_.fill(0.0F);
  writePos_ = 0;
  currentGain_ = 1.0F;
}

void MasterLimiter::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

bool MasterLimiter::isEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

void MasterLimiter::setCeilingDb(float ceilingDb) noexcept {
  ceilingDb = std::clamp(ceilingDb, kMinCeilingDb, kMaxCeilingDb);
  ceilingDb_.store(ceilingDb, std::memory_order_relaxed);
}

float MasterLimiter::ceilingDb() const noexcept {
  return ceilingDb_.load(std::memory_order_relaxed);
}

void MasterLimiter::setReleaseMs(float releaseMs) noexcept {
  releaseMs = std::clamp(releaseMs, 5.0F, 500.0F);
  releaseMs_.store(releaseMs, std::memory_order_relaxed);
}

float MasterLimiter::releaseMs() const noexcept {
  return releaseMs_.load(std::memory_order_relaxed);
}

void MasterLimiter::setLookaheadMs(float lookaheadMs) noexcept {
  lookaheadMs = std::clamp(lookaheadMs, 0.0F, 5.0F);
  lookaheadMs_.store(lookaheadMs, std::memory_order_relaxed);
}

float MasterLimiter::lookaheadMs() const noexcept {
  return lookaheadMs_.load(std::memory_order_relaxed);
}

float MasterLimiter::currentGainReduction() const noexcept {
  return currentGain_;
}

void MasterLimiter::updateCoefficients() noexcept {
  const float cDb = ceilingDb_.load(std::memory_order_relaxed);
  ceilingLinear_ = dbToLinear(cDb);

  const float laMs = lookaheadMs_.load(std::memory_order_relaxed);
  const auto sr = static_cast<float>(sampleRate_);
  const int dSamples = static_cast<int>(std::round(laMs * 0.001F * sr));
  delaySamples_ = std::clamp(dSamples, 0, static_cast<int>(kMaxDelaySamples - 1));

  if (delaySamples_ > 0) {
    attackCoeff_ = 1.0F - std::exp(-1.0F / static_cast<float>(delaySamples_));
  } else {
    attackCoeff_ = 1.0F;
  }

  const float relMs = releaseMs_.load(std::memory_order_relaxed);
  const float relSamples = std::max(1.0F, relMs * 0.001F * sr);
  releaseCoeff_ = 1.0F - std::exp(-1.0F / relSamples);
}

void MasterLimiter::process(float* left, float* right, int numSamples) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0) {
    return;
  }

  updateCoefficients();

  const bool isEnabled = enabled_.load(std::memory_order_relaxed);
  const float ceil = ceilingLinear_;
  const int delay = delaySamples_;
  constexpr std::size_t ringSize = kMaxDelaySamples;

  for (int i = 0; i < numSamples; ++i) {
    const float inL = left[i];
    const float inR = right[i];

    // Store in circular delay buffer
    delayL_[writePos_] = inL;
    delayR_[writePos_] = inR;

    // Peak detection on incoming non-delayed audio
    const float peak = std::max(std::abs(inL), std::abs(inR));
    float targetGain = 1.0F;
    if (isEnabled && peak > ceil && ceil > 0.0F) {
      targetGain = ceil / peak;
    }

    // Attack / release gain smoothing
    if (targetGain < currentGain_) {
      currentGain_ += attackCoeff_ * (targetGain - currentGain_);
    } else {
      currentGain_ += releaseCoeff_ * (targetGain - currentGain_);
    }

    // Read delayed sample
    const std::size_t readPos = (writePos_ + ringSize - static_cast<std::size_t>(delay)) % ringSize;
    float outL = delayL_[readPos] * currentGain_;
    float outR = delayR_[readPos] * currentGain_;

    // Safety brickwall ceiling clamp
    if (isEnabled) {
      outL = std::clamp(outL, -ceil, ceil);
      outR = std::clamp(outR, -ceil, ceil);
    }

    left[i] = outL;
    right[i] = outR;

    writePos_ = (writePos_ + 1) % ringSize;
  }
}

}  // namespace zyron::audio
