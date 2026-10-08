// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/OutputStage.hpp"

#include <algorithm>

namespace zyron::audio {

// RT
void OutputStage::render(float* const* outputs, int numChannels, int numSamples) noexcept {
  if (outputs == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  float* const mono = outputs[0];
  if (mono == nullptr) {
    // No channel to render into: silence the others rather than let the API replay their previous contents.
    for (int channel = 1; channel < numChannels; ++channel) {
      if (outputs[channel] != nullptr) {
        std::fill_n(outputs[channel], numSamples, 0.0F);
      }
    }
    return;
  }

  tone_.render(mono, numSamples);
  for (int channel = 1; channel < numChannels; ++channel) {
    if (outputs[channel] != nullptr) {
      std::copy_n(mono, numSamples, outputs[channel]);
    }
  }
}

}  // namespace zyron::audio
