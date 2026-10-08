// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/KeylockRenderer.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

void KeylockRenderer::prepare(double sampleRate) {
  const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
  stretcher_.prepare(rate, static_cast<int>(kChunk * TimeStretcher::kMaxRatio) + 8);
  maxIn_ = stretcher_.maxPrimeLength();
  inL_.assign(static_cast<std::size_t>(maxIn_), 0.0F);
  inR_.assign(static_cast<std::size_t>(maxIn_), 0.0F);
  transposeCoeff_ = static_cast<float>(1.0 - std::exp(-static_cast<double>(kChunk) / (rate * 0.015)));
  primed_ = false;
  fifoPos_ = kChunk;
  appliedSemitones_ = 1.0e9F;
  currentSemitones_ = targetSemitones_;
}

double KeylockRenderer::latencySourceFrames(double ratio) const noexcept {
  return static_cast<double>(stretcher_.inputLatency()) + ratio * static_cast<double>(stretcher_.outputLatency());
}

// RT
void KeylockRenderer::applyTranspose(bool immediate) noexcept {
  if (immediate) {
    currentSemitones_ = targetSemitones_;
  } else {
    currentSemitones_ += transposeCoeff_ * (targetSemitones_ - currentSemitones_);
    if (std::fabs(targetSemitones_ - currentSemitones_) < 2.0e-3F) {
      currentSemitones_ = targetSemitones_;
    }
  }
  if (currentSemitones_ != appliedSemitones_) {
    appliedSemitones_ = currentSemitones_;
    stretcher_.setTranspose(currentSemitones_ == 0.0F ? 1.0F : std::exp2(currentSemitones_ / 12.0F));
  }
}

// RT
void KeylockRenderer::prime(SourceReader reader, void* context, double headFrame, double srcStep,
                            double ratio) noexcept {
  const int frames = std::min(stretcher_.primeLength(ratio), maxIn_);
  reader(context, headFrame, srcStep, frames, inL_.data(), inR_.data());
  applyTranspose(true);
  const float* planes[2] = {inL_.data(), inR_.data()};
  stretcher_.prime(planes, frames, ratio);
  feed_ = headFrame + static_cast<double>(frames) * srcStep;
  inFrac_ = 0.0;
  fifoPos_ = kChunk;
  primed_ = true;
}

// RT
void KeylockRenderer::produceChunk(SourceReader reader, void* context, double srcStep, double ratio) noexcept {
  ratio = std::clamp(ratio, TimeStretcher::kMinRatio, TimeStretcher::kMaxRatio);
  const double total = inFrac_ + static_cast<double>(kChunk) * ratio;
  int frames = static_cast<int>(total);
  inFrac_ = total - static_cast<double>(frames);
  frames = std::clamp(frames, 1, maxIn_);

  reader(context, feed_, srcStep, frames, inL_.data(), inR_.data());
  feed_ += static_cast<double>(frames) * srcStep;

  applyTranspose(false);
  const float* planes[2] = {inL_.data(), inR_.data()};
  float* outs[2] = {fifoL_.data(), fifoR_.data()};
  stretcher_.process(planes, frames, outs, kChunk);
  fifoPos_ = 0;
}

// RT
void KeylockRenderer::nextFrame(SourceReader reader, void* context, double srcStep, double ratio, float& left,
                                float& right) noexcept {
  if (fifoPos_ >= kChunk) {
    produceChunk(reader, context, srcStep, ratio);
  }
  left = fifoL_[static_cast<std::size_t>(fifoPos_)];
  right = fifoR_[static_cast<std::size_t>(fifoPos_)];
  ++fifoPos_;
}

}  // namespace zyron::audio
