// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/DjFilter.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

DjFilter::DjFilter() {
  prepare(48000.0);
}

void DjFilter::prepare(double sampleRate) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  rampCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.010F));

  for (auto& f : svf_) {
    f.prepare(sampleRate_);
  }
  reset();
}

void DjFilter::reset() noexcept {
  for (auto& f : svf_) {
    f.reset();
  }
  currentKnob_ = targetKnob_.load(std::memory_order_relaxed);
  currentQ_ = targetQ_.load(std::memory_order_relaxed);
}

void DjFilter::setFilter(float knobPosition) noexcept {
  knobPosition = std::clamp(knobPosition, -1.0F, 1.0F);
  targetKnob_.store(knobPosition, std::memory_order_relaxed);
}

void DjFilter::setResonance(float q) noexcept {
  q = std::clamp(q, 0.5F, 5.0F);
  targetQ_.store(q, std::memory_order_relaxed);
}

void DjFilter::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  const float targetK = targetKnob_.load(std::memory_order_relaxed);
  const float targetRes = targetQ_.load(std::memory_order_relaxed);
  const int activeChannels = std::min(numChannels, 2);

  for (int i = 0; i < numSamples; ++i) {
    currentKnob_ += rampCoeff_ * (targetK - currentKnob_);
    currentQ_ += rampCoeff_ * (targetRes - currentQ_);

    const float k = currentKnob_;
    const float q = currentQ_;

    // Bypass deadband around center position
    if (std::abs(k) < 0.01F) {
      continue;
    }

    float cutoff = 1000.0F;
    bool isHighpass = false;

    if (k < 0.0F) {
      // Low-pass: sweeps from 20 kHz down to 20 Hz
      const float norm = -k;
      cutoff = 20000.0F * std::pow(20.0F / 20000.0F, norm);
      isHighpass = false;
    } else {
      // High-pass: sweeps from 20 Hz up to 20 kHz
      const float norm = k;
      cutoff = 20.0F * std::pow(20000.0F / 20.0F, norm);
      isHighpass = true;
    }

    for (int ch = 0; ch < activeChannels; ++ch) {
      if (channels[ch] == nullptr) {
        continue;
      }

      svf_[static_cast<std::size_t>(ch)].setParameters(cutoff, q);
      const auto out = svf_[static_cast<std::size_t>(ch)].process(channels[ch][i]);
      channels[ch][i] = isHighpass ? out.highpass : out.lowpass;
    }
  }
}

}  // namespace zyron::audio
