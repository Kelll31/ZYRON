// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

#include "Audio/DSP/OutputGate.hpp"

using zyron::audio::OutputGate;

namespace {

constexpr double kRate = 48000.0;

/// Runs `blocks` blocks of constant 1.0 stereo through the gate and returns the last left sample.
float runBlocks(OutputGate& gate, int blocks, int blockSize, bool& allFinite) {
  std::vector<float> left(static_cast<std::size_t>(blockSize));
  std::vector<float> right(static_cast<std::size_t>(blockSize));
  float last = 0.0F;
  for (int b = 0; b < blocks; ++b) {
    std::fill(left.begin(), left.end(), 1.0F);
    std::fill(right.begin(), right.end(), 1.0F);
    float* channels[2] = {left.data(), right.data()};
    gate.process(channels, 2, blockSize);
    for (int i = 0; i < blockSize; ++i) {
      allFinite = allFinite && std::isfinite(left[static_cast<std::size_t>(i)]) &&
                  std::isfinite(right[static_cast<std::size_t>(i)]);
    }
    last = left.back();
  }
  return last;
}

}  // namespace

TEST_CASE("OutputGate starts closed after prepare and opens through the ramp", "[audio][outputgate]") {
  OutputGate gate;
  gate.prepare(kRate);
  CHECK(gate.isClosed());
  CHECK_FALSE(gate.suspended());

  bool finite = true;
  std::vector<float> data(64, 1.0F);
  float* channels[1] = {data.data()};
  gate.process(channels, 1, 64);
  CHECK(data.front() < 0.1F);  // starts near silence, not at full level
  CHECK(data.back() > data.front());
  CHECK_FALSE(gate.isClosed());

  const float settled = runBlocks(gate, 40, 256, finite);  // ~213 ms >> 5 ms constant
  CHECK(settled == 1.0F);
  CHECK(finite);
}

TEST_CASE("OutputGate fast path leaves an open gate untouched", "[audio][outputgate]") {
  OutputGate gate;
  gate.prepare(kRate);
  bool finite = true;
  runBlocks(gate, 100, 256, finite);

  std::vector<float> data(256);
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = std::sin(static_cast<float>(i) * 0.1F);
  }
  const std::vector<float> original = data;
  float* channels[1] = {data.data()};
  gate.process(channels, 1, 256);
  CHECK(data == original);  // bit-identical
}

TEST_CASE("OutputGate fades to closed without discontinuities when suspended", "[audio][outputgate]") {
  OutputGate gate;
  gate.prepare(kRate);
  bool finite = true;
  runBlocks(gate, 100, 256, finite);
  REQUIRE_FALSE(gate.isClosed());

  gate.setSuspended(true);
  CHECK(gate.suspended());

  // 5 ms time constant, closed below -80 dB: ln(1e4) * 5 ms = 46 ms. Allow 60 ms.
  const int maxSamples = static_cast<int>(0.060 * kRate);
  int processed = 0;
  float previous = 1.0F;
  float maxStep = 0.0F;
  while (!gate.isClosed() && processed < maxSamples) {
    std::array<float, 32> block{};
    block.fill(1.0F);
    float* channels[1] = {block.data()};
    gate.process(channels, 1, 32);
    for (const float s : block) {
      finite = finite && std::isfinite(s);
      maxStep = std::max(maxStep, std::abs(previous - s));
      previous = s;
      CHECK(s <= 1.0F);
      CHECK(s >= 0.0F);
    }
    processed += 32;
  }
  CHECK(gate.isClosed());
  CHECK(finite);
  CHECK(maxStep < 0.01F);  // smooth: never a hard cut

  // Once closed the output is exactly silent.
  std::array<float, 64> block{};
  block.fill(1.0F);
  float* channels[1] = {block.data()};
  gate.process(channels, 1, 64);
  for (const float s : block) {
    CHECK(s == 0.0F);
  }
  CHECK(gate.isClosed());
}

TEST_CASE("OutputGate reopens after resume", "[audio][outputgate]") {
  OutputGate gate;
  gate.prepare(kRate);
  bool finite = true;
  runBlocks(gate, 100, 256, finite);
  gate.setSuspended(true);
  runBlocks(gate, 40, 256, finite);
  REQUIRE(gate.isClosed());

  gate.setSuspended(false);
  const float settled = runBlocks(gate, 40, 256, finite);
  CHECK(settled == 1.0F);
  CHECK_FALSE(gate.isClosed());
  CHECK(finite);
}

TEST_CASE("OutputGate tolerates null channels, empty blocks and an invalid rate", "[audio][outputgate]") {
  OutputGate gate;
  gate.prepare(0.0);  // falls back to 48 kHz instead of producing NaN coefficients
  gate.process(nullptr, 2, 64);

  std::vector<float> data(16, 1.0F);
  float* channels[2] = {nullptr, data.data()};
  gate.process(channels, 2, 16);
  gate.process(channels, 0, 16);
  gate.process(channels, 2, 0);
  for (const float s : data) {
    CHECK(std::isfinite(s));
  }
}
