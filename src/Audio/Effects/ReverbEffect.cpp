// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Effects/ReverbEffect.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

const std::array<ParameterDescriptor, ReverbEffect::kNumParams> ReverbEffect::kDescriptors = {{
    {"room_size", "Room Size", 0.0F, 0.98F, 0.75F, "%"},
    {"damping", "Damping", 0.0F, 1.0F, 0.25F, "%"},
    {"width", "Stereo Width", 0.0F, 1.0F, 1.0F, "%"},
}};

void ReverbEffect::CombFilter::init(int size) {
  bufferSize = std::max(size, 1);
  buffer.assign(static_cast<std::size_t>(bufferSize), 0.0F);
  bufferIndex = 0;
  filterStore = 0.0F;
}

void ReverbEffect::CombFilter::reset() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0F);
  bufferIndex = 0;
  filterStore = 0.0F;
}

float ReverbEffect::CombFilter::process(float input, float feedback, float damp) noexcept {
  const float output = buffer[bufferIndex];
  filterStore = (output * (1.0F - damp)) + (filterStore * damp);
  buffer[bufferIndex] = input + (filterStore * feedback);
  if (++bufferIndex >= bufferSize) {
    bufferIndex = 0;
  }
  return output;
}

void ReverbEffect::AllpassFilter::init(int size) {
  bufferSize = std::max(size, 1);
  buffer.assign(static_cast<std::size_t>(bufferSize), 0.0F);
  bufferIndex = 0;
}

void ReverbEffect::AllpassFilter::reset() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0F);
  bufferIndex = 0;
}

float ReverbEffect::AllpassFilter::process(float input) noexcept {
  const float bufout = buffer[bufferIndex];
  const float output = -input + bufout;
  buffer[bufferIndex] = input + (bufout * 0.5F);
  if (++bufferIndex >= bufferSize) {
    bufferIndex = 0;
  }
  return output;
}

ReverbEffect::ReverbEffect() {
  prepare(48000.0, 1024);
}

void ReverbEffect::prepare(double sampleRate, [[maybe_unused]] int maxBlockSize) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  const double rateScale = sampleRate_ / 44100.0;

  static constexpr int kCombTunings[kNumCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
  static constexpr int kAllpassTunings[kNumAllpasses] = {556, 441, 341, 225};
  static constexpr int kStereoSpread = 23;

  for (std::size_t i = 0; i < kNumCombs; ++i) {
    const int lenL = static_cast<int>(std::round(kCombTunings[i] * rateScale));
    const int lenR = static_cast<int>(std::round((kCombTunings[i] + kStereoSpread) * rateScale));
    combsL_[i].init(lenL);
    combsR_[i].init(lenR);
  }

  for (std::size_t i = 0; i < kNumAllpasses; ++i) {
    const int lenL = static_cast<int>(std::round(kAllpassTunings[i] * rateScale));
    const int lenR = static_cast<int>(std::round((kAllpassTunings[i] + kStereoSpread) * rateScale));
    allpassesL_[i].init(lenL);
    allpassesR_[i].init(lenR);
  }

  smoothCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.020F));

  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentRoomSize_ = targetRoomSize_.load(std::memory_order_relaxed);
  currentDamping_ = targetDamping_.load(std::memory_order_relaxed);
  currentWidth_ = targetWidth_.load(std::memory_order_relaxed);
}

void ReverbEffect::reset() noexcept {
  for (auto& c : combsL_) {
    c.reset();
  }
  for (auto& c : combsR_) {
    c.reset();
  }
  for (auto& a : allpassesL_) {
    a.reset();
  }
  for (auto& a : allpassesR_) {
    a.reset();
  }
  currentDryWet_ = targetDryWet_.load(std::memory_order_relaxed);
  currentRoomSize_ = targetRoomSize_.load(std::memory_order_relaxed);
  currentDamping_ = targetDamping_.load(std::memory_order_relaxed);
  currentWidth_ = targetWidth_.load(std::memory_order_relaxed);
}

void ReverbEffect::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

bool ReverbEffect::isEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

void ReverbEffect::setDryWet(float mixLinear) noexcept {
  targetDryWet_.store(std::clamp(mixLinear, 0.0F, 1.0F), std::memory_order_relaxed);
}

float ReverbEffect::dryWet() const noexcept {
  return targetDryWet_.load(std::memory_order_relaxed);
}

std::span<const ParameterDescriptor> ReverbEffect::parameters() const noexcept {
  return kDescriptors;
}

void ReverbEffect::setParameter(std::size_t index, float value) noexcept {
  switch (index) {
    case 0:
      setRoomSize(value);
      break;
    case 1:
      setDamping(value);
      break;
    case 2:
      setStereoWidth(value);
      break;
    default:
      break;
  }
}

float ReverbEffect::parameter(std::size_t index) const noexcept {
  switch (index) {
    case 0:
      return roomSize();
    case 1:
      return damping();
    case 2:
      return stereoWidth();
    default:
      return 0.0F;
  }
}

void ReverbEffect::setRoomSize(float size) noexcept {
  targetRoomSize_.store(std::clamp(size, 0.0F, 0.98F), std::memory_order_relaxed);
}

float ReverbEffect::roomSize() const noexcept {
  return targetRoomSize_.load(std::memory_order_relaxed);
}

void ReverbEffect::setDamping(float damp) noexcept {
  targetDamping_.store(std::clamp(damp, 0.0F, 1.0F), std::memory_order_relaxed);
}

float ReverbEffect::damping() const noexcept {
  return targetDamping_.load(std::memory_order_relaxed);
}

void ReverbEffect::setStereoWidth(float width) noexcept {
  targetWidth_.store(std::clamp(width, 0.0F, 1.0F), std::memory_order_relaxed);
}

float ReverbEffect::stereoWidth() const noexcept {
  return targetWidth_.load(std::memory_order_relaxed);
}

void ReverbEffect::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }

  const float targetRs = targetRoomSize_.load(std::memory_order_relaxed);
  const float targetDamp = targetDamping_.load(std::memory_order_relaxed);
  const float targetWid = targetWidth_.load(std::memory_order_relaxed);
  const float targetDw = targetDryWet_.load(std::memory_order_relaxed);

  float* outL = channels[0];
  float* outR = (numChannels > 1 && channels[1] != nullptr) ? channels[1] : nullptr;

  // Fixed gain scaling to avoid comb accumulation clipping
  constexpr float kInputGain = 0.015F;
  constexpr float kWetGain = 3.0F;

  for (int i = 0; i < numSamples; ++i) {
    currentRoomSize_ += smoothCoeff_ * (targetRs - currentRoomSize_);
    currentDamping_ += smoothCoeff_ * (targetDamp - currentDamping_);
    currentWidth_ += smoothCoeff_ * (targetWid - currentWidth_);
    currentDryWet_ += smoothCoeff_ * (targetDw - currentDryWet_);

    const float dryL = outL[i];
    const float dryR = (outR != nullptr) ? outR[i] : dryL;
    const float inSignal = (dryL + dryR) * kInputGain;

    // Accumulate parallel comb outputs
    float sumL = 0.0F;
    float sumR = 0.0F;
    for (std::size_t c = 0; c < kNumCombs; ++c) {
      sumL += combsL_[c].process(inSignal, currentRoomSize_, currentDamping_);
      sumR += combsR_[c].process(inSignal, currentRoomSize_, currentDamping_);
    }

    // Pass through cascaded allpass diffusers
    for (std::size_t a = 0; a < kNumAllpasses; ++a) {
      sumL = allpassesL_[a].process(sumL);
      sumR = allpassesR_[a].process(sumR);
    }

    // Stereo width matrixing
    const float wet1 = currentWidth_ * 0.5F + 0.5F;
    const float wet2 = (1.0F - currentWidth_) * 0.5F;
    const float wetL = (sumL * wet1 + sumR * wet2) * kWetGain;
    const float wetR = (sumR * wet1 + sumL * wet2) * kWetGain;

    const float dw = currentDryWet_;
    outL[i] = (1.0F - dw) * dryL + dw * wetL;
    if (outR != nullptr) {
      outR[i] = (1.0F - dw) * dryR + dw * wetR;
    }
  }
}

}  // namespace zyron::audio
