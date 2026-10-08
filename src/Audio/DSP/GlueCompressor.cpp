// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/GlueCompressor.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

namespace {

float timeConstantCoeff(double sampleRate, float milliseconds) noexcept {
  return static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * static_cast<double>(milliseconds) * 0.001)));
}

/// Gain reduction in dB for a level `levelDb` (soft knee, ratio 2:1).
float computeReductionDb(float levelDb) noexcept {
  const float over = levelDb - GlueCompressor::kThresholdDb;
  constexpr float kSlope = 1.0F - 1.0F / GlueCompressor::kRatio;
  constexpr float kKnee = GlueCompressor::kKneeDb;
  if (2.0F * over <= -kKnee) {
    return 0.0F;
  }
  if (2.0F * over < kKnee) {
    const float x = over + kKnee * 0.5F;
    return kSlope * x * x / (2.0F * kKnee);
  }
  return kSlope * over;
}

}  // namespace

void GlueCompressor::prepare(double sampleRate) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  detectorCoeff_ = timeConstantCoeff(sampleRate_, kDetectorMs);
  attackCoeff_ = timeConstantCoeff(sampleRate_, kAttackMs);
  releaseCoeff_ = timeConstantCoeff(sampleRate_, kReleaseMs);
  reset();
}

void GlueCompressor::reset() noexcept {
  meanSquare_ = 0.0F;
  smoothedReductionDb_ = 0.0F;
  reductionDb_.store(0.0F, std::memory_order_relaxed);
}

// RT
void GlueCompressor::process(float* left, float* right, int numSamples) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0) {
    return;
  }
  const bool active = enabled_.load(std::memory_order_relaxed);
  constexpr float kDbPerNeper = 8.685889638F;  // 20 / ln(10)

  // Nothing to do while off and fully released.
  if (!active && smoothedReductionDb_ < 1.0e-4F) {
    smoothedReductionDb_ = 0.0F;
    reductionDb_.store(0.0F, std::memory_order_relaxed);
    return;
  }

  for (int i = 0; i < numSamples; ++i) {
    const float l = left[i];
    const float r = right[i];
    const float energy = std::max(l * l, r * r);
    meanSquare_ += detectorCoeff_ * (energy - meanSquare_);

    float targetDb = 0.0F;
    if (active && meanSquare_ > 1.0e-9F) {
      targetDb = computeReductionDb(10.0F * std::log10(meanSquare_));
    }
    const float coeff = targetDb > smoothedReductionDb_ ? attackCoeff_ : releaseCoeff_;
    smoothedReductionDb_ += coeff * (targetDb - smoothedReductionDb_);

    const float gain = std::exp(-smoothedReductionDb_ / kDbPerNeper);
    left[i] = l * gain;
    right[i] = r * gain;
  }
  reductionDb_.store(smoothedReductionDb_, std::memory_order_relaxed);
}

}  // namespace zyron::audio
