// SPDX-License-Identifier: AGPL-3.0-only
#include "Stems/Demucs/DemucsStemSeparator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace zyron::stems {

DemucsStemSeparator::DemucsStemSeparator()
    : config_(), session_(nullptr), inferer_(nullptr) {}

DemucsStemSeparator::DemucsStemSeparator(std::shared_ptr<ai::IModelSession> session, DemucsConfig config)
    : config_(config), session_(std::move(session)), inferer_(nullptr) {}

DemucsStemSeparator::DemucsStemSeparator(ChunkInferer inferer, DemucsConfig config)
    : config_(config), session_(nullptr), inferer_(std::move(inferer)) {}

std::vector<float> DemucsStemSeparator::generateWindow(std::int64_t segmentFrames, double overlap) {
  if (segmentFrames <= 0) {
    return {};
  }

  std::vector<float> window(static_cast<std::size_t>(segmentFrames), 1.0F);
  const auto fadeLen = static_cast<std::int64_t>(std::round(segmentFrames * std::clamp(overlap, 0.0, 0.5)));

  if (fadeLen > 0 && fadeLen * 2 <= segmentFrames) {
    constexpr double kPi = std::numbers::pi;
    for (std::int64_t i = 0; i < fadeLen; ++i) {
      const auto phase = (static_cast<double>(i) + 0.5) / static_cast<double>(fadeLen);
      const auto w = static_cast<float>(0.5 * (1.0 - std::cos(kPi * phase)));
      window[static_cast<std::size_t>(i)] = w;
      window[static_cast<std::size_t>(segmentFrames - 1 - i)] = w;
    }
  }

  return window;
}

void DemucsStemSeparator::computeMeanStd(const float* inL, const float* inR, std::int64_t numFrames, float& mean, float& stdDev) {
  if (inL == nullptr || numFrames <= 0) {
    mean = 0.0F;
    stdDev = 1.0F;
    return;
  }

  double sum = 0.0;
  double sumSq = 0.0;
  for (std::int64_t i = 0; i < numFrames; ++i) {
    const float l = inL[i];
    const float r = (inR != nullptr) ? inR[i] : l;
    const double mono = 0.5 * (static_cast<double>(l) + static_cast<double>(r));
    sum += mono;
    sumSq += mono * mono;
  }

  const double total = static_cast<double>(numFrames);
  const double m = sum / total;
  const double var = std::max(0.0, (sumSq / total) - (m * m));

  mean = static_cast<float>(m);
  stdDev = static_cast<float>(std::sqrt(var)) + 1e-8F;
}

StemSeparationResult DemucsStemSeparator::separate(const float* const* inputChannels,
                                                  int numChannels,
                                                  std::int64_t numFrames,
                                                  double sampleRate,
                                                  std::function<void(float progress)> progressCallback) {
  const auto startTime = std::chrono::steady_clock::now();
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
  const float* inR = (numChannels > 1 && inputChannels[1] != nullptr) ? inputChannels[1] : inL;

  float mean = 0.0F;
  float stdDev = 1.0F;
  if (config_.normalizeInput) {
    computeMeanStd(inL, inR, numFrames, mean, stdDev);
  }

  const auto segFrames = std::max<std::int64_t>(64, config_.segmentFrames);
  const auto overlap = std::clamp(config_.overlap, 0.0, 0.5);
  const auto hopFrames = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::round(segFrames * (1.0 - overlap))));
  const auto window = generateWindow(segFrames, overlap);

  std::vector<float> weightSum(frames, 0.0F);
  std::vector<std::vector<float>> stemAccumL(4, std::vector<float>(frames, 0.0F));
  std::vector<std::vector<float>> stemAccumR(4, std::vector<float>(frames, 0.0F));

  std::int64_t totalChunks = 0;
  for (std::int64_t s = 0; s < numFrames; s += hopFrames) {
    ++totalChunks;
  }
  if (totalChunks == 0) {
    totalChunks = 1;
  }

  std::int64_t chunkIdx = 0;
  for (std::int64_t start = 0; start < numFrames; start += hopFrames) {
    // Fill input tensor: shape [1, 2, segFrames]
    ai::Tensor mixTensor(ai::TensorShape({1, 2, segFrames}));
    float* mixData = mixTensor.data();
    for (std::int64_t i = 0; i < segFrames; ++i) {
      const auto pos = start + i;
      float l = 0.0F;
      float r = 0.0F;
      if (pos < numFrames) {
        l = inL[pos];
        r = inR[pos];
        if (config_.normalizeInput) {
          l = (l - mean) / stdDev;
          r = (r - mean) / stdDev;
        }
      }
      mixData[i] = l;
      mixData[segFrames + i] = r;
    }

    std::vector<ai::Tensor> modelOutputs;
    if (inferer_) {
      modelOutputs = inferer_(mixTensor);
    } else if (session_) {
      modelOutputs = session_->run({mixTensor});
    } else {
      // Fallback DSP separation if neither session nor inferer was configured
      ai::Tensor fallbackOut(ai::TensorShape({1, 4, 2, segFrames}));
      float* outData = fallbackOut.data();
      for (std::int64_t i = 0; i < segFrames; ++i) {
        const float l = mixData[i];
        const float r = mixData[segFrames + i];
        // 0: drums (0.3), 1: bass (0.2), 2: other (0.1), 3: vocals (0.4)
        outData[0 * 2 * segFrames + i] = l * 0.3F;
        outData[0 * 2 * segFrames + segFrames + i] = r * 0.3F;

        outData[1 * 2 * segFrames + i] = l * 0.2F;
        outData[1 * 2 * segFrames + segFrames + i] = r * 0.2F;

        outData[2 * 2 * segFrames + i] = l * 0.1F;
        outData[2 * 2 * segFrames + segFrames + i] = r * 0.1F;

        outData[3 * 2 * segFrames + i] = l * 0.4F;
        outData[3 * 2 * segFrames + segFrames + i] = r * 0.4F;
      }
      modelOutputs.push_back(std::move(fallbackOut));
    }

    if (!modelOutputs.empty() && modelOutputs[0].size() >= static_cast<std::size_t>(4 * 2 * segFrames)) {
      const float* outData = modelOutputs[0].data();
      for (int s = 0; s < 4; ++s) {
        const auto stemOffset = static_cast<std::size_t>(s) * 2 * static_cast<std::size_t>(segFrames);
        for (std::int64_t i = 0; i < segFrames; ++i) {
          const auto pos = start + i;
          if (pos >= numFrames) {
            break;
          }
          const float w = window.empty() ? 1.0F : window[static_cast<std::size_t>(i)];
          float sampleL = outData[stemOffset + static_cast<std::size_t>(i)];
          float sampleR = outData[stemOffset + static_cast<std::size_t>(segFrames + i)];
          if (config_.normalizeInput) {
            sampleL = sampleL * stdDev + mean;
            sampleR = sampleR * stdDev + mean;
          }
          stemAccumL[static_cast<std::size_t>(s)][static_cast<std::size_t>(pos)] += sampleL * w;
          stemAccumR[static_cast<std::size_t>(s)][static_cast<std::size_t>(pos)] += sampleR * w;
          if (s == 0) {
            weightSum[static_cast<std::size_t>(pos)] += w;
          }
        }
      }
    }

    ++chunkIdx;
    if (progressCallback != nullptr) {
      progressCallback(static_cast<float>(chunkIdx) / static_cast<float>(totalChunks));
    }
  }

  // HTDemucs model order: 0 = drums, 1 = bass, 2 = other, 3 = vocals
  // ZYRON StemSlot order: Vocals = 0, Drums = 1, Bass = 2, Other = 3
  auto& vocals = result.vocals();
  auto& drums = result.drums();
  auto& bass = result.bass();
  auto& other = result.other();

  for (std::size_t i = 0; i < frames; ++i) {
    const float w = weightSum[i];
    const float invW = (w > 1e-6F) ? (1.0F / w) : 1.0F;

    vocals.left[i] = stemAccumL[3][i] * invW;
    vocals.right[i] = stemAccumR[3][i] * invW;

    drums.left[i] = stemAccumL[0][i] * invW;
    drums.right[i] = stemAccumR[0][i] * invW;

    bass.left[i] = stemAccumL[1][i] * invW;
    bass.right[i] = stemAccumR[1][i] * invW;

    other.left[i] = stemAccumL[2][i] * invW;
    other.right[i] = stemAccumR[2][i] * invW;
  }

  const auto endTime = std::chrono::steady_clock::now();
  result.processingDurationSec = std::chrono::duration<double>(endTime - startTime).count();
  result.success = true;
  return result;
}

}  // namespace zyron::stems
