// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace zyron::test {

// ==============================================================================
// 1. Synthetic Signal Generators
// ==============================================================================

/// Generates a pure sine tone with specified frequency, duration and initial phase.
[[nodiscard]] std::vector<float> generateSine(double sampleRate, double frequencyHz, double durationSec,
                                              float amplitude = 1.0F, double initialPhaseRad = 0.0);

/// Generates a single unit impulse (delta) at the given sample position.
[[nodiscard]] std::vector<float> generateImpulse(int lengthSamples, int impulsePosition = 0, float amplitude = 1.0F);

/// Generates deterministic white noise in [-amplitude, amplitude] using a seeded PRNG.
[[nodiscard]] std::vector<float> generateNoise(int lengthSamples, float amplitude = 1.0F, std::uint32_t seed = 42);

/// Generates a linear frequency sweep (chirp) from startFreqHz to endFreqHz.
[[nodiscard]] std::vector<float> generateSweep(double sampleRate, double startFreqHz, double endFreqHz,
                                               double durationSec, float amplitude = 1.0F);

/// Generates an isochronous click track with clicks at the specified BPM and initial offset.
/// Each click is an audio-frequency burst with a smooth half-sine envelope.
[[nodiscard]] std::vector<float> generateClickTrack(double sampleRate, double bpm, double durationSec,
                                                    double offsetSec = 0.0, double clickDurationSec = 0.005,
                                                    double clickFreqHz = 1500.0);

/// Generates a constant DC signal of the given length.
[[nodiscard]] std::vector<float> generateDc(int lengthSamples, float level = 1.0F);

// ==============================================================================
// 2. Audio Verification & DSP Analysis Probes
// ==============================================================================

/// Returns true if all samples are finite (not NaN and not Inf).
[[nodiscard]] bool isFinite(const std::vector<float>& buffer) noexcept;

/// Returns true if all samples have absolute magnitude <= ceiling.
[[nodiscard]] bool isWithinClip(const std::vector<float>& buffer, float ceiling = 1.0F) noexcept;

/// Returns the maximum absolute amplitude in the buffer starting from startIndex.
[[nodiscard]] float maxAbsolute(const std::vector<float>& buffer, std::size_t startIndex = 0) noexcept;

/// Computes the maximum sample-by-sample absolute difference between buffers a and b.
/// If sizes differ, returns infinity.
[[nodiscard]] float nullTestMaxDiff(const std::vector<float>& a, const std::vector<float>& b) noexcept;

struct CrossCorrelationResult {
  int bestLag{0};
  float maxCorrelation{0.0F};
};

/// Computes normalized cross-correlation between a and b for lags in [-maxLag, maxLag].
/// Returns the lag corresponding to the peak correlation.
[[nodiscard]] CrossCorrelationResult crossCorrelation(const std::vector<float>& a, const std::vector<float>& b,
                                                      int maxLag);

/// Measures the peak magnitude of the signal at a specific frequency using the Goertzel algorithm.
/// For an unwindowed sine wave with amplitude A over an integer number of cycles, returns approximately A.
[[nodiscard]] float measureMagnitudeAt(const std::vector<float>& buffer, double sampleRate, double targetFreqHz);

// ==============================================================================
// 3. Offline Rendering & Block-Size Independence
// ==============================================================================

/// Standard set of block sizes specified by the testing guideline (including powers of 2 and odd sizes).
[[nodiscard]] const std::vector<int>& standardTestBlockSizes();

/// Renders audio in chunks of blockSize by invoking `renderFn(float* outBlock, int numSamples)`.
template <typename RenderBlockFn>
[[nodiscard]] std::vector<float> renderBlocks(RenderBlockFn&& renderFn, int totalSamples, int blockSize) {
  std::vector<float> output(static_cast<std::size_t>(totalSamples), 0.0F);
  for (int done = 0; done < totalSamples;) {
    const int n = std::min(blockSize, totalSamples - done);
    renderFn(output.data() + done, n);
    done += n;
  }
  return output;
}

/// Verifies that rendering totalSamples produces block-size independent results.
/// Factory function `makeRenderer()` returns an object or lambda that can be called as `(float* out, int numSamples)`.
/// Compares output against a reference render (refBlockSize) and returns the maximum difference found across all sizes.
template <typename FactoryFn>
[[nodiscard]] float testBlockSizeIndependence(FactoryFn&& makeRenderer, int totalSamples,
                                              const std::vector<int>& blockSizes = standardTestBlockSizes(),
                                              int refBlockSize = 128) {
  auto refRenderer = makeRenderer();
  const auto ref = renderBlocks(refRenderer, totalSamples, refBlockSize);

  float maxDiffAcrossSizes = 0.0F;
  for (const int bs : blockSizes) {
    if (bs == refBlockSize) {
      continue;
    }
    auto testRenderer = makeRenderer();
    const auto testOut = renderBlocks(testRenderer, totalSamples, bs);
    const float diff = nullTestMaxDiff(ref, testOut);
    maxDiffAcrossSizes = std::max(maxDiffAcrossSizes, diff);
  }
  return maxDiffAcrossSizes;
}

}  // namespace zyron::test
