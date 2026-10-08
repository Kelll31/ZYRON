// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/Mixer.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

namespace {

float dbToLinear(float db) noexcept {
  if (db <= -59.0F) {
    return 0.0F;  // Full mute
  }
  return std::pow(10.0F, db * 0.05F);
}

}  // namespace

Mixer::Mixer() {
  channelAssign_[0].store(CrossfaderAssign::Left, std::memory_order_relaxed);
  channelAssign_[1].store(CrossfaderAssign::Right, std::memory_order_relaxed);
  channelAssign_[2].store(CrossfaderAssign::Left, std::memory_order_relaxed);
  channelAssign_[3].store(CrossfaderAssign::Right, std::memory_order_relaxed);

  for (auto& cue : cueEnabled_) {
    cue.store(false, std::memory_order_relaxed);
  }

  prepare(48000.0);
}

void Mixer::prepare(double sampleRate) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
  // 5 ms crossfader smoothing
  rampCoeffCrossfader_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.005F));
  // 10 ms master gain smoothing
  rampCoeffGain_ = 1.0F - std::exp(-1.0F / (static_cast<float>(sampleRate_) * 0.010F));

  fxHits_.prepare(sampleRate_);
  glue_.prepare(sampleRate_);
  masterLimiter_.prepare(sampleRate_);
  reset();
}

void Mixer::reset() noexcept {
  currentCrossfader_ = targetCrossfader_.load(std::memory_order_relaxed);
  currentMasterGainLinear_ = targetMasterGainLinear_.load(std::memory_order_relaxed);
  fxHits_.reset();
  glue_.reset();
  masterLimiter_.reset();
  masterPeakLeft_.store(0.0F, std::memory_order_relaxed);
  masterPeakRight_.store(0.0F, std::memory_order_relaxed);
}

void Mixer::setCrossfader(float position) noexcept {
  position = std::clamp(position, -1.0F, 1.0F);
  targetCrossfader_.store(position, std::memory_order_relaxed);
}

float Mixer::crossfader() const noexcept {
  return targetCrossfader_.load(std::memory_order_relaxed);
}

void Mixer::setCrossfaderCurve(CrossfaderCurve curve) noexcept {
  curve_.store(curve, std::memory_order_relaxed);
}

CrossfaderCurve Mixer::crossfaderCurve() const noexcept {
  return curve_.load(std::memory_order_relaxed);
}

void Mixer::setChannelAssign(int channel, CrossfaderAssign assign) noexcept {
  if (channel >= 0 && channel < kMaxChannels) {
    channelAssign_[static_cast<std::size_t>(channel)].store(assign, std::memory_order_relaxed);
  }
}

CrossfaderAssign Mixer::channelAssign(int channel) const noexcept {
  if (channel >= 0 && channel < kMaxChannels) {
    return channelAssign_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
  }
  return CrossfaderAssign::Thru;
}

void Mixer::setMasterGainDb(float gainDb) noexcept {
  gainDb = std::clamp(gainDb, kMinGainDb, kMaxGainDb);
  const float lin = dbToLinear(gainDb);
  targetMasterGainLinear_.store(lin, std::memory_order_relaxed);
}

float Mixer::masterGainDb() const noexcept {
  const float lin = targetMasterGainLinear_.load(std::memory_order_relaxed);
  if (lin <= 0.0F) {
    return kMinGainDb;
  }
  return 20.0F * std::log10(lin);
}

void Mixer::setMasterProcessing(bool glue, bool limiter) noexcept {
  glue_.setEnabled(glue);
  masterLimiter_.setEnabled(limiter);
}

void Mixer::setCue(int channel, bool enabled) noexcept {
  if (channel >= 0 && channel < kMaxChannels) {
    cueEnabled_[static_cast<std::size_t>(channel)].store(enabled, std::memory_order_relaxed);
  }
}

bool Mixer::isCue(int channel) const noexcept {
  if (channel >= 0 && channel < kMaxChannels) {
    return cueEnabled_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
  }
  return false;
}

float Mixer::masterPeakLeft() const noexcept {
  return masterPeakLeft_.load(std::memory_order_relaxed);
}

float Mixer::masterPeakRight() const noexcept {
  return masterPeakRight_.load(std::memory_order_relaxed);
}

void Mixer::computeCrossfaderGains(float position, float& gainLeft, float& gainRight) const noexcept {
  const float p = std::clamp((position + 1.0F) * 0.5F, 0.0F, 1.0F);
  const CrossfaderCurve curve = curve_.load(std::memory_order_relaxed);

  switch (curve) {
    case CrossfaderCurve::Linear: {
      gainLeft = 1.0F - p;
      gainRight = p;
      break;
    }
    case CrossfaderCurve::ConstantPower: {
      constexpr float kHalfPi = 1.5707963267948966F;
      const float angle = p * kHalfPi;
      gainLeft = std::cos(angle);
      gainRight = std::sin(angle);
      break;
    }
    case CrossfaderCurve::Cut: {
      constexpr float kCutLag = 0.05F;  // 5% cut threshold
      gainLeft = (p > (1.0F - kCutLag)) ? std::max(0.0F, (1.0F - p) / kCutLag) : 1.0F;
      gainRight = (p < kCutLag) ? std::max(0.0F, p / kCutLag) : 1.0F;
      break;
    }
  }
}

void Mixer::process(const float* leftA, const float* rightA, const float* leftB, const float* rightB, float* masterLeft,
                    float* masterRight, int numSamples) noexcept {
  const float* inputsL[2] = {leftA, leftB};
  const float* inputsR[2] = {rightA, rightB};
  process(inputsL, inputsR, 2, masterLeft, masterRight, numSamples);
}

void Mixer::process(const float* const* channelLefts, const float* const* channelRights, int numChannels,
                    float* masterLeft, float* masterRight, int numSamples) noexcept {
  if (masterLeft == nullptr || masterRight == nullptr || numSamples <= 0) {
    return;
  }

  const float targetCf = targetCrossfader_.load(std::memory_order_relaxed);
  const float targetGain = targetMasterGainLinear_.load(std::memory_order_relaxed);
  const int activeChannels = std::min(numChannels, kMaxChannels);

  std::array<CrossfaderAssign, kMaxChannels> assigns{};
  for (int ch = 0; ch < activeChannels; ++ch) {
    assigns[static_cast<std::size_t>(ch)] =
        channelAssign_[static_cast<std::size_t>(ch)].load(std::memory_order_relaxed);
  }

  for (int i = 0; i < numSamples; ++i) {
    currentCrossfader_ += rampCoeffCrossfader_ * (targetCf - currentCrossfader_);
    currentMasterGainLinear_ += rampCoeffGain_ * (targetGain - currentMasterGainLinear_);

    float gLeft = 1.0F;
    float gRight = 1.0F;
    computeCrossfaderGains(currentCrossfader_, gLeft, gRight);

    float sumL = 0.0F;
    float sumR = 0.0F;

    for (int ch = 0; ch < activeChannels; ++ch) {
      const auto uCh = static_cast<std::size_t>(ch);
      const float inL = (channelLefts != nullptr && channelLefts[uCh] != nullptr) ? channelLefts[uCh][i] : 0.0F;
      const float inR = (channelRights != nullptr && channelRights[uCh] != nullptr) ? channelRights[uCh][i] : 0.0F;

      float mul = 1.0F;
      switch (assigns[uCh]) {
        case CrossfaderAssign::Left:
          mul = gLeft;
          break;
        case CrossfaderAssign::Right:
          mul = gRight;
          break;
        case CrossfaderAssign::Thru:
          mul = 1.0F;
          break;
      }

      sumL += inL * mul;
      sumR += inR * mul;
    }

    // A NaN or infinity from anywhere upstream must never reach the output (or poison the compressor and the limiter).
    const float outL = sumL * currentMasterGainLinear_;
    const float outR = sumR * currentMasterGainLinear_;
    masterLeft[i] = std::isfinite(outL) ? outL : 0.0F;
    masterRight[i] = std::isfinite(outR) ? outR : 0.0F;
  }

  // Performance hits join the bus here, so the glue and the limiter see them like any other signal.
  fxHits_.process(masterLeft, masterRight, numSamples);

  // Glue compressor, then the brickwall limiter
  glue_.process(masterLeft, masterRight, numSamples);
  masterLimiter_.process(masterLeft, masterRight, numSamples);

  // Meter peak levels for telemetry
  float peakL = 0.0F;
  float peakR = 0.0F;
  for (int i = 0; i < numSamples; ++i) {
    peakL = std::max(peakL, std::abs(masterLeft[i]));
    peakR = std::max(peakR, std::abs(masterRight[i]));
  }
  masterPeakLeft_.store(peakL, std::memory_order_relaxed);
  masterPeakRight_.store(peakR, std::memory_order_relaxed);
}

void Mixer::processCue(const float* const* channelLefts, const float* const* channelRights, int numChannels,
                       float* cueLeft, float* cueRight, int numSamples) noexcept {
  if (cueLeft == nullptr || cueRight == nullptr || numSamples <= 0) {
    return;
  }

  const int activeChannels = std::min(numChannels, kMaxChannels);

  std::array<bool, kMaxChannels> cues{};
  for (int ch = 0; ch < activeChannels; ++ch) {
    cues[static_cast<std::size_t>(ch)] = cueEnabled_[static_cast<std::size_t>(ch)].load(std::memory_order_relaxed);
  }

  for (int i = 0; i < numSamples; ++i) {
    float sumL = 0.0F;
    float sumR = 0.0F;

    for (int ch = 0; ch < activeChannels; ++ch) {
      const auto uCh = static_cast<std::size_t>(ch);
      if (!cues[uCh]) {
        continue;
      }

      const float inL = (channelLefts != nullptr && channelLefts[uCh] != nullptr) ? channelLefts[uCh][i] : 0.0F;
      const float inR = (channelRights != nullptr && channelRights[uCh] != nullptr) ? channelRights[uCh][i] : 0.0F;

      sumL += inL;
      sumR += inR;
    }

    cueLeft[i] = sumL;
    cueRight[i] = sumR;
  }
}

}  // namespace zyron::audio
