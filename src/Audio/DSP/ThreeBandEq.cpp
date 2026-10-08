// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/ThreeBandEq.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

namespace {

float dbToLinear(float db) noexcept {
  if (db <= -59.0F) {
    return 0.0F;  // Full kill
  }
  return std::pow(10.0F, db * 0.05F);
}

}  // namespace

ThreeBandEq::ThreeBandEq() {
  targetGainLinear_[0].store(1.0F, std::memory_order_relaxed);
  targetGainLinear_[1].store(1.0F, std::memory_order_relaxed);
  targetGainLinear_[2].store(1.0F, std::memory_order_relaxed);
  prepare(48000.0);
}

void ThreeBandEq::prepare(double sampleRate) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  // 10 ms smoothing time constant
  rampCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.010F));

  for (std::size_t ch = 0; ch < 2; ++ch) {
    svfLowA_[ch].prepare(sampleRate_);
    svfLowA_[ch].setParameters(kLowCrossoverHz, 0.7071F);
    svfLowB_LP_[ch].prepare(sampleRate_);
    svfLowB_LP_[ch].setParameters(kLowCrossoverHz, 0.7071F);
    svfLowB_HP_[ch].prepare(sampleRate_);
    svfLowB_HP_[ch].setParameters(kLowCrossoverHz, 0.7071F);

    svfHighA_[ch].prepare(sampleRate_);
    svfHighA_[ch].setParameters(kHighCrossoverHz, 0.7071F);
    svfHighB_LP_[ch].prepare(sampleRate_);
    svfHighB_LP_[ch].setParameters(kHighCrossoverHz, 0.7071F);
    svfHighB_HP_[ch].prepare(sampleRate_);
    svfHighB_HP_[ch].setParameters(kHighCrossoverHz, 0.7071F);

    svfHighAP_[ch].prepare(sampleRate_);
    svfHighAP_[ch].setParameters(kHighCrossoverHz, 0.7071F);
  }
  reset();
}

void ThreeBandEq::reset() noexcept {
  for (std::size_t ch = 0; ch < 2; ++ch) {
    svfLowA_[ch].reset();
    svfLowB_LP_[ch].reset();
    svfLowB_HP_[ch].reset();
    svfHighA_[ch].reset();
    svfHighB_LP_[ch].reset();
    svfHighB_HP_[ch].reset();
    svfHighAP_[ch].reset();
  }
  currentGainLinear_[0] = targetGainLinear_[0].load(std::memory_order_relaxed);
  currentGainLinear_[1] = targetGainLinear_[1].load(std::memory_order_relaxed);
  currentGainLinear_[2] = targetGainLinear_[2].load(std::memory_order_relaxed);
}

void ThreeBandEq::setBandDb(core::EqBand band, float gainDb) noexcept {
  gainDb = std::clamp(gainDb, -60.0F, 12.0F);
  const float lin = dbToLinear(gainDb);
  const auto idx = static_cast<std::size_t>(core::index(band));
  if (idx < targetGainLinear_.size()) {
    targetGainLinear_[idx].store(lin, std::memory_order_relaxed);
  }
}

void ThreeBandEq::setLowDb(float gainDb) noexcept {
  setBandDb(core::EqBand::Low, gainDb);
}

void ThreeBandEq::setMidDb(float gainDb) noexcept {
  setBandDb(core::EqBand::Mid, gainDb);
}

void ThreeBandEq::setHighDb(float gainDb) noexcept {
  setBandDb(core::EqBand::High, gainDb);
}

void ThreeBandEq::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  const float targetLow = targetGainLinear_[0].load(std::memory_order_relaxed);
  const float targetMid = targetGainLinear_[1].load(std::memory_order_relaxed);
  const float targetHigh = targetGainLinear_[2].load(std::memory_order_relaxed);

  const int activeChannels = std::min(numChannels, 2);

  for (int i = 0; i < numSamples; ++i) {
    // Parameter smoothing once per sample across all channels
    currentGainLinear_[0] += rampCoeff_ * (targetLow - currentGainLinear_[0]);
    currentGainLinear_[1] += rampCoeff_ * (targetMid - currentGainLinear_[1]);
    currentGainLinear_[2] += rampCoeff_ * (targetHigh - currentGainLinear_[2]);

    const float gL = currentGainLinear_[0];
    const float gM = currentGainLinear_[1];
    const float gH = currentGainLinear_[2];

    for (int ch = 0; ch < activeChannels; ++ch) {
      if (channels[ch] == nullptr) {
        continue;
      }

      const float in = channels[ch][i];
      const auto uCh = static_cast<std::size_t>(ch);

      // Low crossover (250 Hz, LR4):
      const auto out1A = svfLowA_[uCh].process(in);
      const float low1 = svfLowB_LP_[uCh].process(out1A.lowpass).lowpass;
      const float high1 = svfLowB_HP_[uCh].process(out1A.highpass).highpass;

      // High crossover (2500 Hz, LR4):
      const auto out2A = svfHighA_[uCh].process(high1);
      const float midSignal = svfHighB_LP_[uCh].process(out2A.lowpass).lowpass;
      const float highSignal = svfHighB_HP_[uCh].process(out2A.highpass).highpass;

      // Allpass phase alignment for Low band at high crossover frequency
      const float lowSignal = svfHighAP_[uCh].process(low1).allpass;

      // Recombine scaled bands
      channels[ch][i] = lowSignal * gL + midSignal * gM + highSignal * gH;
    }
  }
}

}  // namespace zyron::audio
