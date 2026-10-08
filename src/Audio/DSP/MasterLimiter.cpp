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
  resetGainHistory();
}

void MasterLimiter::resetGainHistory() noexcept {
  historyWindow_ = delaySamples_ + 1;
  sampleCounter_ = 0;
  queueHead_ = 0;
  queueCount_ = 0;
  minHistory_.fill(1.0F);
  minHistorySum_ = static_cast<double>(historyWindow_);
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

  const float relMs = releaseMs_.load(std::memory_order_relaxed);
  const float relSamples = std::max(1.0F, relMs * 0.001F * sr);
  releaseCoeff_ = 1.0F - std::exp(-1.0F / relSamples);
}

void MasterLimiter::process(float* left, float* right, int numSamples) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0) {
    return;
  }

  updateCoefficients();
  if (historyWindow_ != delaySamples_ + 1) {
    resetGainHistory();  // the lookahead was changed: the window the history was built for is gone
  }

  const bool isEnabled = enabled_.load(std::memory_order_relaxed);
  const float ceil = ceilingLinear_;
  const int delay = delaySamples_;
  const int window = historyWindow_;
  const double windowSize = static_cast<double>(window);
  constexpr std::size_t ringSize = kMaxDelaySamples;

  for (int i = 0; i < numSamples; ++i) {
    const float inL = left[i];
    const float inR = right[i];

    // Store in circular delay buffer
    delayL_[writePos_] = inL;
    delayR_[writePos_] = inR;

    // Gain this sample needs so that it stays under the ceiling (1 = nothing to do)
    const float peak = std::max(std::abs(inL), std::abs(inR));
    float required = 1.0F;
    if (isEnabled && peak > ceil && ceil > 0.0F) {
      required = ceil / peak;
    }

    // Minimum over the lookahead window (monotonic queue): the gain must already be down when the peak comes out
    while (queueCount_ > 0 && queueValue_[(queueHead_ + queueCount_ - 1) & kHistoryMask] >= required) {
      --queueCount_;
    }
    const std::size_t slot = (queueHead_ + queueCount_) & kHistoryMask;
    queueIndex_[slot] = sampleCounter_;
    queueValue_[slot] = required;
    ++queueCount_;
    while (queueIndex_[queueHead_] <= sampleCounter_ - window) {
      queueHead_ = (queueHead_ + 1) & kHistoryMask;
      --queueCount_;
    }
    const float windowMin = queueValue_[queueHead_];

    // Moving average of that minimum over the same length: a smooth ramp down that has reached the minimum exactly when
    // the peak leaves the delay line
    const std::size_t newest = static_cast<std::size_t>(sampleCounter_) & kHistoryMask;
    const std::size_t oldest = static_cast<std::size_t>(sampleCounter_ - window) & kHistoryMask;
    minHistorySum_ += static_cast<double>(windowMin) - static_cast<double>(minHistory_[oldest]);
    minHistory_[newest] = windowMin;
    ++sampleCounter_;
    const float smoothed = static_cast<float>(minHistorySum_ / windowSize);

    // Down: immediately with the smoothed ramp (never above what the peaks need). Up: exponential release.
    if (smoothed < currentGain_) {
      currentGain_ = smoothed;
    } else {
      currentGain_ += releaseCoeff_ * (smoothed - currentGain_);
    }

    // Read delayed sample
    const std::size_t readPos = (writePos_ + ringSize - static_cast<std::size_t>(delay)) % ringSize;
    float outL = delayL_[readPos] * currentGain_;
    float outR = delayR_[readPos] * currentGain_;

    // Safety clamp (rounding error only: the gain above already keeps the waveform under the ceiling)
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
