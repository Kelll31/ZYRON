// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/ChannelStrip.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

namespace {

float dbToLinear(float db) noexcept {
  return std::pow(10.0F, db * 0.05F);
}

constexpr int kFxBlockSize = 4096;  // the largest block AudioGraph hands to a strip
constexpr float kMaxTrimDb = 12.0F;

}  // namespace

ChannelStrip::ChannelStrip() {
  prepare(48000.0);
}

void ChannelStrip::prepare(double sampleRate) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  rampCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.010F));

  eq_.prepare(sampleRate_);
  filter_.prepare(sampleRate_);
  for (auto& slot : fxSlots_) {
    slot.prepare(sampleRate_, 1024);
  }
  for (auto& unit : fxUnits_) {
    unit.prepare(sampleRate_, kFxBlockSize);
  }
  reset();
}

void ChannelStrip::reset() noexcept {
  eq_.reset();
  filter_.reset();
  for (auto& slot : fxSlots_) {
    slot.reset();
  }
  for (auto& unit : fxUnits_) {
    unit.reset();
  }
  currentTrimLinear_ = targetTrimLinear_.load(std::memory_order_relaxed);
  currentGainLinear_ = targetGainLinear_.load(std::memory_order_relaxed);
  currentVolumeLinear_ = targetVolumeLinear_.load(std::memory_order_relaxed);
  peakLeft_.store(0.0F, std::memory_order_relaxed);
  peakRight_.store(0.0F, std::memory_order_relaxed);
}

void ChannelStrip::setGainDb(float gainDb) noexcept {
  gainDb = std::clamp(gainDb, -24.0F, 12.0F);
  targetGainLinear_.store(dbToLinear(gainDb), std::memory_order_relaxed);
}

void ChannelStrip::setTrackGainTrimDb(float trimDb) noexcept {
  if (!std::isfinite(trimDb)) {
    return;
  }
  trimDb = std::clamp(trimDb, -kMaxTrimDb, kMaxTrimDb);
  trackTrimDb_.store(trimDb, std::memory_order_relaxed);
  targetTrimLinear_.store(dbToLinear(trimDb), std::memory_order_relaxed);
}

void ChannelStrip::setFx(int slot, core::FxType type, bool enabled, float wet, float param, bool postFader) noexcept {
  if (slot < 0 || slot >= kNumFxSlots) {
    return;
  }
  fxUnits_[static_cast<std::size_t>(slot)].configure(type, enabled, wet, param, postFader);
}

void ChannelStrip::setFxBeatSeconds(float beatSeconds) noexcept {
  for (auto& unit : fxUnits_) {
    unit.setBeatSeconds(beatSeconds);
  }
}

const FxUnit& ChannelStrip::fxUnit(int slot) const noexcept {
  return fxUnits_[static_cast<std::size_t>(std::clamp(slot, 0, kNumFxSlots - 1))];
}

void ChannelStrip::setEqDb(core::EqBand band, float gainDb) noexcept {
  eq_.setBandDb(band, gainDb);
}

void ChannelStrip::setLowDb(float gainDb) noexcept {
  eq_.setLowDb(gainDb);
}

void ChannelStrip::setMidDb(float gainDb) noexcept {
  eq_.setMidDb(gainDb);
}

void ChannelStrip::setHighDb(float gainDb) noexcept {
  eq_.setHighDb(gainDb);
}

void ChannelStrip::setFilter(float knobPosition) noexcept {
  filter_.setFilter(knobPosition);
}

void ChannelStrip::setFilterResonance(float q) noexcept {
  filter_.setResonance(q);
}

void ChannelStrip::setVolume(float volumeLinear) noexcept {
  volumeLinear = std::clamp(volumeLinear, 0.0F, 1.0F);
  targetVolumeLinear_.store(volumeLinear, std::memory_order_relaxed);
}

void ChannelStrip::setMute(bool muted) noexcept {
  muted_.store(muted, std::memory_order_relaxed);
}

EffectSlot& ChannelStrip::fxSlot(int slotIndex) noexcept {
  const int idx = std::clamp(slotIndex, 0, kNumFxSlots - 1);
  return fxSlots_[static_cast<std::size_t>(idx)];
}

const EffectSlot& ChannelStrip::fxSlot(int slotIndex) const noexcept {
  const int idx = std::clamp(slotIndex, 0, kNumFxSlots - 1);
  return fxSlots_[static_cast<std::size_t>(idx)];
}

void ChannelStrip::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  const bool isMuted = muted_.load(std::memory_order_relaxed);
  const float targetGain = targetGainLinear_.load(std::memory_order_relaxed);
  const float targetTrim = targetTrimLinear_.load(std::memory_order_relaxed);
  const float targetVol = isMuted ? 0.0F : targetVolumeLinear_.load(std::memory_order_relaxed);
  const int activeChannels = std::min(numChannels, 2);

  // 1. Apply smoothed track trim and input gain
  for (int i = 0; i < numSamples; ++i) {
    currentGainLinear_ += rampCoeff_ * (targetGain - currentGainLinear_);
    currentTrimLinear_ += rampCoeff_ * (targetTrim - currentTrimLinear_);
    const float g = currentGainLinear_ * currentTrimLinear_;
    for (int ch = 0; ch < activeChannels; ++ch) {
      if (channels[ch] != nullptr) {
        channels[ch][i] *= g;
      }
    }
  }

  // 2. 3-Band Isolator / EQ
  eq_.process(channels, numChannels, numSamples);

  // 3. DJ HPF/LPF Filter
  filter_.process(channels, numChannels, numSamples);

  // 4. Free-form FX slots (post-filter, pre-fader per ARCHITECTURE section 7)
  for (auto& slot : fxSlots_) {
    slot.process(channels, numChannels, numSamples);
  }

  // 4b. Effects that sit before the fader
  for (auto& unit : fxUnits_) {
    if (!unit.postFader()) {
      unit.process(channels, numChannels, numSamples);
    }
  }

  // 5. Apply smoothed fader volume
  for (int i = 0; i < numSamples; ++i) {
    currentVolumeLinear_ += rampCoeff_ * (targetVol - currentVolumeLinear_);
    const float v = currentVolumeLinear_;

    if (channels[0] != nullptr) {
      channels[0][i] *= v;
    }
    if (activeChannels > 1 && channels[1] != nullptr) {
      channels[1][i] *= v;
    }
  }

  // 6. Effects after the fader: they only see what the fader lets through, and their tails ring on when it closes
  for (auto& unit : fxUnits_) {
    if (unit.postFader()) {
      unit.process(channels, numChannels, numSamples);
    }
  }

  // 7. Peak meter, after everything that is heard
  float maxL = 0.0F;
  float maxR = 0.0F;
  for (int i = 0; i < numSamples; ++i) {
    if (channels[0] != nullptr) {
      maxL = std::max(maxL, std::abs(channels[0][i]));
    }
    if (activeChannels > 1 && channels[1] != nullptr) {
      maxR = std::max(maxR, std::abs(channels[1][i]));
    }
  }

  // Peak meter update with one-pole ballistics decay
  const float decay = 0.9F;
  const float oldL = peakLeft_.load(std::memory_order_relaxed);
  const float oldR = peakRight_.load(std::memory_order_relaxed);
  peakLeft_.store(std::max(maxL, oldL * decay), std::memory_order_relaxed);
  peakRight_.store(std::max(maxR, oldR * decay), std::memory_order_relaxed);
}

float ChannelStrip::peakLeft() const noexcept {
  return peakLeft_.load(std::memory_order_relaxed);
}

float ChannelStrip::peakRight() const noexcept {
  return peakRight_.load(std::memory_order_relaxed);
}

}  // namespace zyron::audio
