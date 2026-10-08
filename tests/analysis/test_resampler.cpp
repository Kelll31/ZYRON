// SPDX-License-Identifier: AGPL-3.0-only
// AudioResampler: the per-phase table fast path must match a straightforward windowed-sinc reference.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "Analysis/Features/Resampler.hpp"

using zyron::analysis::AudioResampler;

namespace {

/// Deterministic noise plus two tones (LCG, no <random> distribution differences between standard libraries).
std::vector<float> testSignal(std::size_t count, int rate) {
  std::vector<float> signal(count);
  std::uint32_t state = 12345U;
  for (std::size_t i = 0; i < count; ++i) {
    state = state * 1664525U + 1013904223U;
    const float noise = (static_cast<float>(state >> 8) / 8388608.0F - 1.0F) * 0.1F;
    const double t = static_cast<double>(i) / rate;
    signal[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * t) +
                                   0.25 * std::sin(2.0 * std::numbers::pi * 3000.0 * t)) +
                noise;
  }
  return signal;
}

double sincD(double x) {
  if (std::abs(x) < 1e-12) {
    return 1.0;
  }
  const double pix = std::numbers::pi * x;
  return std::sin(pix) / pix;
}

/// Direct evaluation of the documented formula in double precision: Blackman-windowed sinc, cutoff 0.95 * min(1, ratio),
/// kernel half-width radius / min(1, ratio) input samples, normalised by the sum of the taps.
std::vector<float> referenceResample(const std::vector<float>& input, int inRate, int outRate, int radius = 16) {
  const double ratio = static_cast<double>(outRate) / inRate;
  const auto outCount = static_cast<std::size_t>(std::ceil(static_cast<double>(input.size()) * ratio));
  const double cutoff = std::min(1.0, ratio) * 0.95;
  const int width = static_cast<int>(std::ceil(radius / std::min(1.0, ratio)));
  const int last = static_cast<int>(input.size()) - 1;

  std::vector<float> output(outCount);
  for (std::size_t n = 0; n < outCount; ++n) {
    const double pos = static_cast<double>(n) / ratio;
    const int centre = static_cast<int>(std::floor(pos));
    double sum = 0.0;
    double weights = 0.0;
    for (int k = std::max(0, centre - width); k <= std::min(last, centre + width); ++k) {
      const double diff = pos - k;
      const double dist = diff / width;
      if (std::abs(dist) >= 1.0) {
        continue;
      }
      const double window = 0.42 + 0.5 * std::cos(std::numbers::pi * dist) + 0.08 * std::cos(2.0 * std::numbers::pi * dist);
      const double tap = sincD(diff * cutoff) * cutoff * window;
      sum += input[static_cast<std::size_t>(k)] * tap;
      weights += tap;
    }
    output[n] = std::abs(weights) > 1e-6 ? static_cast<float>(sum / weights) : 0.0F;
  }
  return output;
}

void expectMatchesReference(int inRate, int outRate, std::size_t count) {
  const auto input = testSignal(count, inRate);
  const auto expected = referenceResample(input, inRate, outRate);
  const auto actual = AudioResampler::resample(input.data(), input.size(), inRate, outRate);

  REQUIRE(actual.size() == expected.size());
  float maxError = 0.0F;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    REQUIRE(std::isfinite(actual[i]));
    maxError = std::max(maxError, std::abs(actual[i] - expected[i]));
  }
  INFO("rates " << inRate << " -> " << outRate);
  CHECK(maxError < 1e-4F);
}

}  // namespace

TEST_CASE("AudioResampler fast path equals the reference for 44100 -> 22050", "[analysis][resampler]") {
  expectMatchesReference(44100, 22050, 4410);
}

TEST_CASE("AudioResampler fast path equals the reference for 48000 -> 22050", "[analysis][resampler]") {
  expectMatchesReference(48000, 22050, 4800);
}

TEST_CASE("AudioResampler fast path equals the reference for 96000 -> 22050", "[analysis][resampler]") {
  expectMatchesReference(96000, 22050, 9600);
}

TEST_CASE("AudioResampler fast path equals the reference when upsampling", "[analysis][resampler]") {
  expectMatchesReference(22050, 44100, 2205);
}

TEST_CASE("AudioResampler slow path (too many phases) equals the reference", "[analysis][resampler]") {
  // 44100 -> 22051 are coprime: 22051 phases exceed the table limit.
  expectMatchesReference(44100, 22051, 2000);
}

TEST_CASE("AudioResampler preserves a constant signal including the edges", "[analysis][resampler]") {
  const std::vector<float> dc(3000, 0.7F);
  const auto out = AudioResampler::resample(dc.data(), dc.size(), 48000, 22050);
  REQUIRE_FALSE(out.empty());
  for (const float s : out) {
    CHECK(std::abs(s - 0.7F) < 1e-4F);
  }
}

TEST_CASE("AudioResampler output length is ceil(n * ratio)", "[analysis][resampler]") {
  const std::vector<float> data(1001, 0.1F);
  CHECK(AudioResampler::resample(data.data(), data.size(), 44100, 22050).size() == 501);
  CHECK(AudioResampler::resample(data.data(), data.size(), 48000, 22050).size() ==
        static_cast<std::size_t>(std::ceil(1001.0 * 22050.0 / 48000.0)));
}

TEST_CASE("AudioResampler handles signals shorter than the filter kernel", "[analysis][resampler]") {
  const auto input = testSignal(5, 48000);  // the kernel (~35 taps) is cut off on both sides everywhere
  const auto expected = referenceResample(input, 48000, 22050);
  const auto actual = AudioResampler::resample(input.data(), input.size(), 48000, 22050);
  REQUIRE(actual.size() == expected.size());
  for (std::size_t i = 0; i < actual.size(); ++i) {
    CHECK(std::abs(actual[i] - expected[i]) < 1e-4F);
  }

  const float single = 0.25F;
  const auto one = AudioResampler::resample(&single, 1, 48000, 22050);
  REQUIRE(one.size() == 1);
  CHECK(std::abs(one[0] - 0.25F) < 1e-4F);
}

TEST_CASE("AudioResampler returns empty for empty input and copies at equal rates", "[analysis][resampler]") {
  const float sample = 1.0F;
  CHECK(AudioResampler::resample(nullptr, 10, 44100, 22050).empty());
  CHECK(AudioResampler::resample(&sample, 0, 44100, 22050).empty());

  const auto input = testSignal(100, 44100);
  CHECK(AudioResampler::resample(input.data(), input.size(), 44100, 44100) == input);
}

TEST_CASE("AudioResampler rejects non-positive rates", "[analysis][resampler]") {
  CHECK_THROWS_AS(AudioResampler(0, 22050), std::invalid_argument);
  CHECK_THROWS_AS(AudioResampler(44100, -1), std::invalid_argument);
}
