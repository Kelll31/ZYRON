// SPDX-License-Identifier: AGPL-3.0-only
#include "Stems/MockStemSeparator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace zyron::stems {

StemSeparationResult MockStemSeparator::separate(const float* const* inputChannels,
                                                 int numChannels,
                                                 std::int64_t numFrames,
                                                 double sampleRate,
                                                 std::function<void(float progress)> progressCallback) {
  const auto start = std::chrono::steady_clock::now();
  StemSeparationResult result;
  result.sampleRate = sampleRate;
  result.numFrames = numFrames;

  if (inputChannels == nullptr || numChannels <= 0 || numFrames <= 0) {
    result.success = false;
    result.error = "Invalid or empty input audio buffer";
    return result;
  }

  const auto frames = static_cast<std::size_t>(numFrames);
  for (std::size_t s = 0; s < kStemCount; ++s) {
    result.stems[s].resize(frames);
    result.stems[s].sampleRate = sampleRate;
    result.stems[s].channels = std::min(numChannels, 2);
  }

  const float* inL = inputChannels[0];
  const float* inR = (numChannels > 1) ? inputChannels[1] : inputChannels[0];

  auto& vocals = result.vocals();
  auto& drums = result.drums();
  auto& bass = result.bass();
  auto& other = result.other();

  // Simple, stable IIR 1-pole filter state for multi-band splitting
  float bassL = 0.0f;
  float bassR = 0.0f;
  const float bassAlpha = static_cast<float>(1.0 - std::exp(-2.0 * 3.141592653589793 * 250.0 / sampleRate));

  float midL = 0.0f;
  float midR = 0.0f;
  const float midAlpha = static_cast<float>(1.0 - std::exp(-2.0 * 3.141592653589793 * 3500.0 / sampleRate));

  const std::size_t reportInterval = std::max<std::size_t>(1024, frames / 20);

  for (std::size_t i = 0; i < frames; ++i) {
    const float l = inL[i];
    const float r = inR[i];

    // Bass (< 250 Hz)
    bassL += bassAlpha * (l - bassL);
    bassR += bassAlpha * (r - bassR);
    bass.left[i] = bassL;
    bass.right[i] = bassR;

    // Mid band
    midL += midAlpha * (l - midL);
    midR += midAlpha * (r - midR);
    const float midBandL = midL - bassL;
    const float midBandR = midR - bassR;

    // Vocals: center-channel correlated mid band
    const float center = (midBandL + midBandR) * 0.5f;
    vocals.left[i] = center * 0.8f;
    vocals.right[i] = center * 0.8f;

    // Drums: high frequencies (> 3.5 kHz) + punch
    const float highL = l - midL;
    const float highR = r - midR;
    drums.left[i] = highL * 0.7f;
    drums.right[i] = highR * 0.7f;

    // Other: residual component
    other.left[i] = l - (bass.left[i] + vocals.left[i] + drums.left[i]);
    other.right[i] = r - (bass.right[i] + vocals.right[i] + drums.right[i]);

    if (progressCallback && (i % reportInterval == 0)) {
      progressCallback(static_cast<float>(i) / static_cast<float>(frames));
    }
  }

  if (progressCallback) {
    progressCallback(1.0f);
  }

  const auto end = std::chrono::steady_clock::now();
  result.processingDurationSec = std::chrono::duration<double>(end - start).count();
  result.success = true;
  return result;
}

}  // namespace zyron::stems
