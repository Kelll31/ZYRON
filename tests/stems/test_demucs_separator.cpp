// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "AI/Runtime/Tensor.hpp"
#include "Stems/Demucs/DemucsStemSeparator.hpp"
#include "Stems/StemTypes.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron;
using namespace zyron::stems;

TEST_CASE("DemucsStemSeparator: window generation and partition of unity", "[stems][demucs]") {
  SECTION("Empty or invalid segment lengths") {
    auto w = DemucsStemSeparator::generateWindow(0, 0.25);
    CHECK(w.empty());
    auto wNeg = DemucsStemSeparator::generateWindow(-100, 0.25);
    CHECK(wNeg.empty());
  }

  SECTION("Hann fade window values and symmetry") {
    constexpr std::int64_t kSeg = 1000;
    constexpr double kOverlap = 0.25;
    auto w = DemucsStemSeparator::generateWindow(kSeg, kOverlap);
    REQUIRE(w.size() == 1000);

    // Fade is 250 frames
    CHECK_THAT(w[0], WithinAbs(0.0F, 1e-4F));
    CHECK_THAT(w[kSeg - 1], WithinAbs(0.0F, 1e-4F));

    // Interior is 1.0
    CHECK_THAT(w[500], WithinAbs(1.0F, 1e-5F));

    // Symmetry check
    for (std::size_t i = 0; i < 250; ++i) {
      CHECK_THAT(w[i], WithinAbs(w[w.size() - 1 - i], 1e-5F));
    }
  }

  SECTION("Overlap-add partition of unity sum across hop sequence") {
    constexpr std::int64_t kSeg = 512;
    constexpr double kOverlap = 0.25;
    const auto hop = static_cast<std::int64_t>(std::round(kSeg * (1.0 - kOverlap)));
    constexpr std::int64_t kTotalFrames = 2048;

    auto w = DemucsStemSeparator::generateWindow(kSeg, kOverlap);
    std::vector<float> accum(kTotalFrames, 0.0F);

    for (std::int64_t start = 0; start < kTotalFrames; start += hop) {
      for (std::int64_t i = 0; i < kSeg; ++i) {
        if (start + i < kTotalFrames) {
          accum[static_cast<std::size_t>(start + i)] += w[static_cast<std::size_t>(i)];
        }
      }
    }

    // In the interior (after first fade and before last fade), weights sum to 1.0
    const auto interiorStart = static_cast<std::size_t>(kSeg * kOverlap);
    const auto interiorEnd = static_cast<std::size_t>(kTotalFrames - (kSeg * kOverlap));
    for (std::size_t i = interiorStart; i < interiorEnd; ++i) {
      CHECK_THAT(accum[i], WithinAbs(1.0F, 1e-4F));
    }
  }
}

TEST_CASE("DemucsStemSeparator: mean and std audio normalization", "[stems][demucs]") {
  constexpr std::int64_t kFrames = 1000;
  std::vector<float> left(kFrames, 0.0F);
  std::vector<float> right(kFrames, 0.0F);

  // DC offset of 2.0 and sine wave of amp 1.0
  constexpr double kTwoPi = 6.283185307179586;
  for (std::int64_t i = 0; i < kFrames; ++i) {
    const auto val = static_cast<float>(2.0 + std::sin(kTwoPi * static_cast<double>(i) / 100.0));
    left[static_cast<std::size_t>(i)] = val;
    right[static_cast<std::size_t>(i)] = val;
  }

  float mean = 0.0F;
  float stdDev = 1.0F;
  DemucsStemSeparator::computeMeanStd(left.data(), right.data(), kFrames, mean, stdDev);

  CHECK_THAT(mean, WithinAbs(2.0F, 0.05F));
  CHECK_THAT(stdDev, WithinAbs(0.707F, 0.05F));  // RMS of sine is 1 / sqrt(2) ≈ 0.7071
}

TEST_CASE("DemucsStemSeparator: chunked separation and stem slot reordering", "[stems][demucs]") {
  // Use a custom inferer that returns known distinct stems
  // Model order: 0=drums, 1=bass, 2=other, 3=vocals
  DemucsConfig config;
  config.segmentFrames = 256;
  config.overlap = 0.25;
  config.normalizeInput = false;

  int callCount = 0;
  DemucsStemSeparator::ChunkInferer mockInferer = [&](const ai::Tensor& input) -> std::vector<ai::Tensor> {
    ++callCount;
    const auto segFrames = input.shape().dim(2);
    ai::Tensor out(ai::TensorShape({1, 4, 2, segFrames}));
    float* data = out.data();

    // 0: drums = 0.1 * input
    // 1: bass   = 0.2 * input
    // 2: other  = 0.3 * input
    // 3: vocals = 0.4 * input
    for (std::int64_t i = 0; i < segFrames; ++i) {
      const float inL = input[static_cast<std::size_t>(i)];
      const float inR = input[static_cast<std::size_t>(segFrames + i)];

      data[0 * 2 * segFrames + i] = inL * 0.1F;
      data[0 * 2 * segFrames + segFrames + i] = inR * 0.1F;

      data[1 * 2 * segFrames + i] = inL * 0.2F;
      data[1 * 2 * segFrames + segFrames + i] = inR * 0.2F;

      data[2 * 2 * segFrames + i] = inL * 0.3F;
      data[2 * 2 * segFrames + segFrames + i] = inR * 0.3F;

      data[3 * 2 * segFrames + i] = inL * 0.4F;
      data[3 * 2 * segFrames + segFrames + i] = inR * 0.4F;
    }
    return {std::move(out)};
  };

  DemucsStemSeparator separator(mockInferer, config);
  CHECK(separator.modelName() == "HTDemucs");
  CHECK(separator.modelVersion() == "4.0.0");
  CHECK(separator.requiredSampleRate() == 44100.0);

  constexpr std::int64_t kTotalFrames = 600;
  std::vector<float> inL(kTotalFrames, 1.0F);
  std::vector<float> inR(kTotalFrames, 1.0F);
  const float* channels[2] = {inL.data(), inR.data()};

  std::vector<float> progressReports;
  auto result = separator.separate(channels, 2, kTotalFrames, 44100.0, [&](float p) {
    progressReports.push_back(p);
  });

  REQUIRE(result.success);
  CHECK(result.numFrames == kTotalFrames);
  CHECK(callCount > 1);  // Multiple segments processed
  REQUIRE_FALSE(progressReports.empty());
  CHECK_THAT(progressReports.back(), WithinAbs(1.0F, 1e-5F));

  // Verify ZYRON mapping:
  // Vocals = model stem 3 (0.4)
  // Drums  = model stem 0 (0.1)
  // Bass   = model stem 1 (0.2)
  // Other  = model stem 2 (0.3)
  CHECK_THAT(result.vocals().left[100], WithinAbs(0.4F, 1e-4F));
  CHECK_THAT(result.drums().left[100], WithinAbs(0.1F, 1e-4F));
  CHECK_THAT(result.bass().left[100], WithinAbs(0.2F, 1e-4F));
  CHECK_THAT(result.other().left[100], WithinAbs(0.3F, 1e-4F));
}

TEST_CASE("DemucsStemSeparator: invalid inputs handling", "[stems][demucs]") {
  DemucsStemSeparator separator;

  SECTION("Null channels pointer") {
    auto res = separator.separate(nullptr, 2, 512, 44100.0);
    CHECK_FALSE(res.success);
    CHECK_FALSE(res.error.empty());
  }

  SECTION("Zero or negative frames") {
    std::vector<float> buf(128, 0.0F);
    const float* ptrs[1] = {buf.data()};
    auto res = separator.separate(ptrs, 1, 0, 44100.0);
    CHECK_FALSE(res.success);
  }
}
