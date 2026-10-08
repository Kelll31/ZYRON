// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/StemMixer.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

StemMixer::StemMixer() {
  prepare(48000.0);
}

void StemMixer::prepare(double sampleRate) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  rampCoeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate_)));
  if (rampCoeff_ <= 0.0F || rampCoeff_ > 1.0F) {
    rampCoeff_ = 0.005F;
  }

  scratchLeft_.assign(8192, 0.0F);
  scratchRight_.assign(8192, 0.0F);

  for (auto& stem : stems_) {
    stem.eq.prepare(sampleRate_);
    stem.filter.prepare(sampleRate_);
  }
}

void StemMixer::reset() noexcept {
  for (auto& stem : stems_) {
    stem.eq.reset();
    stem.filter.reset();
    stem.hasActiveEqOrFilter = false;
    stem.currentGain = stem.targetVolume.load(std::memory_order_relaxed);
    stem.peakLeft.store(0.0F, std::memory_order_relaxed);
    stem.peakRight.store(0.0F, std::memory_order_relaxed);
  }
}

void StemMixer::setVolume(core::StemKind stem, float volumeLinear) noexcept {
  if (!core::isValid(stem)) {
    return;
  }
  const float clamped = std::clamp(volumeLinear, 0.0F, 1.0F);
  stems_[core::index(stem)].targetVolume.store(clamped, std::memory_order_relaxed);
}

float StemMixer::volume(core::StemKind stem) const noexcept {
  if (!core::isValid(stem)) {
    return 0.0F;
  }
  return stems_[core::index(stem)].targetVolume.load(std::memory_order_relaxed);
}

void StemMixer::setMute(core::StemKind stem, bool muted) noexcept {
  if (!core::isValid(stem)) {
    return;
  }
  stems_[core::index(stem)].muted.store(muted, std::memory_order_relaxed);
}

bool StemMixer::isMuted(core::StemKind stem) const noexcept {
  if (!core::isValid(stem)) {
    return false;
  }
  return stems_[core::index(stem)].muted.load(std::memory_order_relaxed);
}

void StemMixer::setSolo(core::StemKind stem, bool solo) noexcept {
  if (!core::isValid(stem)) {
    return;
  }
  stems_[core::index(stem)].solo.store(solo, std::memory_order_relaxed);
}

bool StemMixer::isSolo(core::StemKind stem) const noexcept {
  if (!core::isValid(stem)) {
    return false;
  }
  return stems_[core::index(stem)].solo.load(std::memory_order_relaxed);
}

void StemMixer::setCue(core::StemKind stem, bool cue) noexcept {
  if (!core::isValid(stem)) {
    return;
  }
  stems_[core::index(stem)].cue.store(cue, std::memory_order_relaxed);
}

bool StemMixer::isCue(core::StemKind stem) const noexcept {
  if (!core::isValid(stem)) {
    return false;
  }
  return stems_[core::index(stem)].cue.load(std::memory_order_relaxed);
}

void StemMixer::setEq(core::StemKind stem, core::EqBand band, float gainDb) noexcept {
  if (!core::isValid(stem)) {
    return;
  }
  auto& s = stems_[core::index(stem)];
  s.eq.setBandDb(band, gainDb);
  s.hasActiveEqOrFilter = true;
}

void StemMixer::setFilter(core::StemKind stem, float bipolarKnob) noexcept {
  if (!core::isValid(stem)) {
    return;
  }
  auto& s = stems_[core::index(stem)];
  s.filter.setFilter(bipolarKnob);
  s.hasActiveEqOrFilter = true;
}

float StemMixer::peakLeft(core::StemKind stem) const noexcept {
  if (!core::isValid(stem)) {
    return 0.0F;
  }
  return stems_[core::index(stem)].peakLeft.load(std::memory_order_relaxed);
}

float StemMixer::peakRight(core::StemKind stem) const noexcept {
  if (!core::isValid(stem)) {
    return 0.0F;
  }
  return stems_[core::index(stem)].peakRight.load(std::memory_order_relaxed);
}

void StemMixer::process(const float* const* stemLefts, const float* const* stemRights, float* masterLeft,
                        float* masterRight, int numSamples) noexcept {
  if (masterLeft == nullptr || masterRight == nullptr || numSamples <= 0) {
    return;
  }

  std::fill_n(masterLeft, numSamples, 0.0F);
  std::fill_n(masterRight, numSamples, 0.0F);

  bool anySolo = false;
  for (std::size_t i = 0; i < core::kStemKindCount; ++i) {
    if (stems_[i].solo.load(std::memory_order_relaxed)) {
      anySolo = true;
      break;
    }
  }

  const int samplesToProcess = std::min(numSamples, static_cast<int>(scratchLeft_.size()));

  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    auto& channel = stems_[s];
    const float* inL = (stemLefts != nullptr) ? stemLefts[s] : nullptr;
    const float* inR = (stemRights != nullptr) ? stemRights[s] : nullptr;

    const bool isSolo = channel.solo.load(std::memory_order_relaxed);
    const bool isMuted = channel.muted.load(std::memory_order_relaxed);
    const bool audible = anySolo ? (isSolo && !isMuted) : !isMuted;
    const float targetGain = audible ? channel.targetVolume.load(std::memory_order_relaxed) : 0.0F;

    if (channel.currentGain <= 1e-5F && targetGain <= 1e-5F) {
      channel.currentGain = 0.0F;
      channel.peakLeft.store(0.0F, std::memory_order_relaxed);
      channel.peakRight.store(0.0F, std::memory_order_relaxed);
      continue;
    }

    float currentGain = channel.currentGain;
    float peakL = 0.0F;
    float peakR = 0.0F;

    if (channel.hasActiveEqOrFilter) {
      // Copy to preallocated scratch buffers for in-place filtering
      for (int i = 0; i < samplesToProcess; ++i) {
        currentGain += rampCoeff_ * (targetGain - currentGain);
        scratchLeft_[static_cast<std::size_t>(i)] = (inL != nullptr ? inL[i] : 0.0F) * currentGain;
        scratchRight_[static_cast<std::size_t>(i)] = (inR != nullptr ? inR[i] : 0.0F) * currentGain;
      }

      float* eqPtrs[2] = {scratchLeft_.data(), scratchRight_.data()};
      channel.eq.process(eqPtrs, 2, samplesToProcess);
      channel.filter.process(eqPtrs, 2, samplesToProcess);

      for (int i = 0; i < samplesToProcess; ++i) {
        const float l = scratchLeft_[static_cast<std::size_t>(i)];
        const float r = scratchRight_[static_cast<std::size_t>(i)];
        masterLeft[i] += l;
        masterRight[i] += r;
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
      }
    } else {
      // Fast path: direct gain ramp and sum
      for (int i = 0; i < samplesToProcess; ++i) {
        currentGain += rampCoeff_ * (targetGain - currentGain);
        const float l = (inL != nullptr ? inL[i] : 0.0F) * currentGain;
        const float r = (inR != nullptr ? inR[i] : 0.0F) * currentGain;
        masterLeft[i] += l;
        masterRight[i] += r;
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
      }
    }

    channel.currentGain = currentGain;
    channel.peakLeft.store(peakL, std::memory_order_relaxed);
    channel.peakRight.store(peakR, std::memory_order_relaxed);
  }
}

void StemMixer::processCue(const float* const* stemLefts, const float* const* stemRights, float* cueLeft,
                           float* cueRight, int numSamples) noexcept {
  if (cueLeft == nullptr || cueRight == nullptr || numSamples <= 0) {
    return;
  }

  std::fill_n(cueLeft, numSamples, 0.0F);
  std::fill_n(cueRight, numSamples, 0.0F);

  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    auto& channel = stems_[s];
    if (!channel.cue.load(std::memory_order_relaxed)) {
      continue;
    }

    const float* inL = (stemLefts != nullptr) ? stemLefts[s] : nullptr;
    const float* inR = (stemRights != nullptr) ? stemRights[s] : nullptr;

    const float vol = channel.targetVolume.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i) {
      if (inL != nullptr) {
        cueLeft[i] += inL[i] * vol;
      }
      if (inR != nullptr) {
        cueRight[i] += inR[i] * vol;
      }
    }
  }
}

void StemMixer::processStems(const float* const* stemLefts, const float* const* stemRights, float* const* outLefts,
                             float* const* outRights, int numSamples) noexcept {
  if (outLefts == nullptr || outRights == nullptr || numSamples <= 0) {
    return;
  }

  bool anySolo = false;
  for (std::size_t i = 0; i < core::kStemKindCount; ++i) {
    if (stems_[i].solo.load(std::memory_order_relaxed)) {
      anySolo = true;
      break;
    }
  }

  const int samplesToProcess = std::min(numSamples, static_cast<int>(scratchLeft_.size()));

  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    auto& channel = stems_[s];
    float* destL = outLefts[s];
    float* destR = outRights[s];
    if (destL == nullptr || destR == nullptr) {
      continue;
    }

    const float* inL = (stemLefts != nullptr) ? stemLefts[s] : nullptr;
    const float* inR = (stemRights != nullptr) ? stemRights[s] : nullptr;

    const bool isSolo = channel.solo.load(std::memory_order_relaxed);
    const bool isMuted = channel.muted.load(std::memory_order_relaxed);
    const bool audible = anySolo ? (isSolo && !isMuted) : !isMuted;
    const float targetGain = audible ? channel.targetVolume.load(std::memory_order_relaxed) : 0.0F;

    if (channel.currentGain <= 1e-5F && targetGain <= 1e-5F) {
      channel.currentGain = 0.0F;
      std::fill_n(destL, numSamples, 0.0F);
      std::fill_n(destR, numSamples, 0.0F);
      channel.peakLeft.store(0.0F, std::memory_order_relaxed);
      channel.peakRight.store(0.0F, std::memory_order_relaxed);
      continue;
    }

    float currentGain = channel.currentGain;
    float peakL = 0.0F;
    float peakR = 0.0F;

    if (channel.hasActiveEqOrFilter) {
      for (int i = 0; i < samplesToProcess; ++i) {
        currentGain += rampCoeff_ * (targetGain - currentGain);
        scratchLeft_[static_cast<std::size_t>(i)] = (inL != nullptr ? inL[i] : 0.0F) * currentGain;
        scratchRight_[static_cast<std::size_t>(i)] = (inR != nullptr ? inR[i] : 0.0F) * currentGain;
      }

      float* eqPtrs[2] = {scratchLeft_.data(), scratchRight_.data()};
      channel.eq.process(eqPtrs, 2, samplesToProcess);
      channel.filter.process(eqPtrs, 2, samplesToProcess);

      for (int i = 0; i < samplesToProcess; ++i) {
        const float l = scratchLeft_[static_cast<std::size_t>(i)];
        const float r = scratchRight_[static_cast<std::size_t>(i)];
        destL[i] = l;
        destR[i] = r;
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
      }
    } else {
      for (int i = 0; i < samplesToProcess; ++i) {
        currentGain += rampCoeff_ * (targetGain - currentGain);
        const float l = (inL != nullptr ? inL[i] : 0.0F) * currentGain;
        const float r = (inR != nullptr ? inR[i] : 0.0F) * currentGain;
        destL[i] = l;
        destR[i] = r;
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
      }
    }

    channel.currentGain = currentGain;
    channel.peakLeft.store(peakL, std::memory_order_relaxed);
    channel.peakRight.store(peakR, std::memory_order_relaxed);
  }
}

}  // namespace zyron::audio
