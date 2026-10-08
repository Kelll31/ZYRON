// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/OutputGate.hpp"

#include <cmath>

namespace zyron::audio {

void OutputGate::prepare(double sampleRate) noexcept {
  const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
  coefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (kSmoothingSeconds * rate)));
  gain_ = 0.0F;
  closed_.store(true, std::memory_order_release);
}

void OutputGate::process(float* const* outputs, int numChannels, int numSamples) noexcept {
  if (outputs == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }
  const float target = suspended_.load(std::memory_order_acquire) ? 0.0F : 1.0F;

  // Fully open and staying open: nothing to do (the common case).
  if (target == 1.0F && gain_ == 1.0F) {
    return;
  }

  for (int i = 0; i < numSamples; ++i) {
    gain_ += coefficient_ * (target - gain_);
    for (int ch = 0; ch < numChannels; ++ch) {
      if (outputs[ch] != nullptr) {
        outputs[ch][i] *= gain_;
      }
    }
  }

  // Snap at the ends so the fast path above is reached and the fade reports completion.
  if (target == 1.0F && gain_ > 1.0F - kClosedGain) {
    gain_ = 1.0F;
  } else if (target == 0.0F && gain_ < kClosedGain) {
    gain_ = 0.0F;
  }
  closed_.store(target == 0.0F && gain_ == 0.0F, std::memory_order_release);
}

}  // namespace zyron::audio
