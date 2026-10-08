// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Effects/EffectSlot.hpp"

#include <algorithm>

namespace zyron::audio {

void EffectSlot::prepare(double sampleRate, int maxBlockSize) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  maxBlockSize_ = std::max(maxBlockSize, 16);

  if (effect_ != nullptr) {
    effect_->prepare(sampleRate_, maxBlockSize_);
  }
}

void EffectSlot::reset() noexcept {
  if (effect_ != nullptr) {
    effect_->reset();
  }
}

void EffectSlot::setEffect(std::unique_ptr<Effect> effect) noexcept {
  if (effect != nullptr) {
    effect->prepare(sampleRate_, maxBlockSize_);
  }
  effect_ = std::move(effect);
  activeEffect_.store(effect_.get(), std::memory_order_release);
}

Effect* EffectSlot::effect() const noexcept {
  return activeEffect_.load(std::memory_order_acquire);
}

void EffectSlot::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
  Effect* fx = activeEffect_.load(std::memory_order_relaxed);
  if (fx != nullptr) {
    fx->setEnabled(enabled);
  }
}

bool EffectSlot::isEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

void EffectSlot::setDryWet(float mixLinear) noexcept {
  const float clamped = std::clamp(mixLinear, 0.0F, 1.0F);
  dryWet_.store(clamped, std::memory_order_relaxed);
  Effect* fx = activeEffect_.load(std::memory_order_relaxed);
  if (fx != nullptr) {
    fx->setDryWet(clamped);
  }
}

float EffectSlot::dryWet() const noexcept {
  return dryWet_.load(std::memory_order_relaxed);
}

void EffectSlot::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }

  Effect* fx = activeEffect_.load(std::memory_order_acquire);
  if (fx != nullptr && fx->isEnabled()) {
    fx->process(channels, numChannels, numSamples);
  }
}

}  // namespace zyron::audio
