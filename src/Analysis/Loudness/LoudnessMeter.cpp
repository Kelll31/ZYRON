// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Loudness/LoudnessMeter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace zyron::analysis {

namespace {

constexpr double kShelfFrequencyHz = 1681.974450955533;
constexpr double kShelfGainDb = 3.999843853973347;
constexpr double kShelfQ = 0.7071752369554196;
constexpr double kHighPassFrequencyHz = 38.13547087602444;
constexpr double kHighPassQ = 0.5003270373238773;
constexpr double kShelfVbExponent = 0.4996667741545416;

constexpr double kLoudnessOffsetDb = -0.691;
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kRelativeGateLu = -10.0;
constexpr double kSubBlockSec = 0.1;  // 400 ms blocks advance by 100 ms (75 % overlap)
constexpr int kSubBlocksPerBlock = 4;

struct Biquad {
  double b0{1.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0};
  double z1{0.0}, z2{0.0};  // transposed direct form II

  double process(double x) {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

double blockLufs(double meanSquareSum) { return kLoudnessOffsetDb + 10.0 * std::log10(meanSquareSum); }

/// Sum over channels of the K-weighted mean square of each 100 ms sub-block.
std::vector<double> subBlockPowers(const float* const* channels, int channelCount, std::size_t frames, std::size_t sub,
                                   const KWeightingCoefficients& k) {
  const std::size_t count = frames / sub;
  std::vector<double> power(count, 0.0);
  for (int c = 0; c < channelCount; ++c) {
    Biquad shelf{k.shelfB[0], k.shelfB[1], k.shelfB[2], k.shelfA[0], k.shelfA[1]};
    Biquad highPass{k.highPassB[0], k.highPassB[1], k.highPassB[2], k.highPassA[0], k.highPassA[1]};
    const float* in = channels[c];
    for (std::size_t block = 0; block < count; ++block) {
      double sum = 0.0;
      for (std::size_t i = block * sub, end = i + sub; i < end; ++i) {
        const double y = highPass.process(shelf.process(static_cast<double>(in[i])));
        sum += y * y;
      }
      power[block] += sum / static_cast<double>(sub);
    }
  }
  return power;
}

}  // namespace

KWeightingCoefficients kWeightingCoefficients(double sampleRate) {
  KWeightingCoefficients k;
  {
    const double vh = std::pow(10.0, kShelfGainDb / 20.0);
    const double vb = std::pow(vh, kShelfVbExponent);
    const double w = std::tan(std::numbers::pi * kShelfFrequencyHz / sampleRate);
    const double a0 = 1.0 + w / kShelfQ + w * w;
    k.shelfB = {(vh + vb * w / kShelfQ + w * w) / a0, 2.0 * (w * w - vh) / a0, (vh - vb * w / kShelfQ + w * w) / a0};
    k.shelfA = {2.0 * (w * w - 1.0) / a0, (1.0 - w / kShelfQ + w * w) / a0};
  }
  {
    const double w = std::tan(std::numbers::pi * kHighPassFrequencyHz / sampleRate);
    const double a0 = 1.0 + w / kHighPassQ + w * w;
    k.highPassB = {1.0, -2.0, 1.0};
    k.highPassA = {2.0 * (w * w - 1.0) / a0, (1.0 - w / kHighPassQ + w * w) / a0};
  }
  return k;
}

LoudnessResult measureIntegratedLoudness(const float* const* channels, int channelCount, std::size_t frames,
                                         int sampleRate) {
  LoudnessResult result;
  if (channels == nullptr || channelCount <= 0 || sampleRate <= 0) {
    return result;
  }
  const auto sub = static_cast<std::size_t>(kSubBlockSec * sampleRate);
  if (sub == 0 || frames / sub < static_cast<std::size_t>(kSubBlocksPerBlock)) {
    return result;
  }
  const std::vector<double> subPower =
      subBlockPowers(channels, channelCount, frames, sub, kWeightingCoefficients(static_cast<double>(sampleRate)));

  // 400 ms blocks: the mean power of four consecutive sub-blocks.
  std::vector<double> blocks;
  blocks.reserve(subPower.size());
  for (std::size_t i = 0; i + kSubBlocksPerBlock <= subPower.size(); ++i) {
    double sum = 0.0;
    for (int j = 0; j < kSubBlocksPerBlock; ++j) {
      sum += subPower[i + static_cast<std::size_t>(j)];
    }
    blocks.push_back(sum / kSubBlocksPerBlock);
  }

  const auto gatedMean = [&](double thresholdLufs, double& mean) {
    double sum = 0.0;
    std::size_t n = 0;
    for (const double block : blocks) {
      if (block > 0.0 && blockLufs(block) > thresholdLufs) {
        sum += block;
        ++n;
      }
    }
    mean = n > 0 ? sum / static_cast<double>(n) : 0.0;
    return n > 0;
  };
  double absoluteMean = 0.0;
  if (!gatedMean(kAbsoluteGateLufs, absoluteMean)) {
    return result;  // silence
  }
  double relativeMean = 0.0;
  if (!gatedMean(std::max(kAbsoluteGateLufs, blockLufs(absoluteMean) + kRelativeGateLu), relativeMean)) {
    return result;
  }
  result.valid = true;
  result.lufs = blockLufs(relativeMean);
  return result;
}

}  // namespace zyron::analysis
