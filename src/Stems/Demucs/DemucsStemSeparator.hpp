// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AI/Runtime/AIRuntime.hpp"
#include "AI/Runtime/Tensor.hpp"
#include "Stems/StemSeparator.hpp"
#include "Stems/StemTypes.hpp"

namespace zyron::stems {

/// Configuration for HTDemucs chunked separation (SPEC section 32, ADR-0006).
struct DemucsConfig {
  /// Segment length in sample frames (HTDemucs fixed export segment: 7.8 s @ 44.1 kHz = 343 980 frames).
  std::int64_t segmentFrames{343980};

  /// Overlap ratio between adjacent segments (typically 0.25 = 25% overlap).
  double overlap{0.25};

  /// Compute device index (0 = GPU 0, 1 = GPU 1, -1 = CPU).
  int deviceIndex{0};

  /// Whether to apply mean/std normalisation per Demucs standard contract.
  bool normalizeInput{true};
};

/// Deep-learning HTDemucs stem separator supporting ONNX Runtime, CUDA EP, and fallback runtimes (SPEC section 32, 33, ADR-0006).
class DemucsStemSeparator final : public StemSeparator {
 public:
  /// Callback type for executing a single 7.8 s segment inference.
  /// Input tensor shape: [1, 2, segmentFrames].
  /// Output tensor shape: [1, 4, 2, segmentFrames] (order: drums, bass, other, vocals).
  using ChunkInferer = std::function<std::vector<ai::Tensor>(const ai::Tensor& inputMix)>;

  DemucsStemSeparator();
  explicit DemucsStemSeparator(std::shared_ptr<ai::IModelSession> session, DemucsConfig config = {});
  explicit DemucsStemSeparator(ChunkInferer inferer, DemucsConfig config = {});
  ~DemucsStemSeparator() override = default;

  [[nodiscard]] std::string modelName() const override { return "HTDemucs"; }
  [[nodiscard]] std::string modelVersion() const override { return "4.0.0"; }
  [[nodiscard]] double requiredSampleRate() const override { return 44100.0; }

  [[nodiscard]] const DemucsConfig& config() const noexcept { return config_; }
  void setConfig(DemucsConfig config) noexcept { config_ = config; }

  /// Separates input stereo audio into 4 stems using overlap-add segment processing.
  StemSeparationResult separate(const float* const* inputChannels,
                                int numChannels,
                                std::int64_t numFrames,
                                double sampleRate,
                                std::function<void(float progress)> progressCallback = nullptr) override;

  /// Generates a tapered Hann fade overlap-add window satisfying partition of unity.
  [[nodiscard]] static std::vector<float> generateWindow(std::int64_t segmentFrames, double overlap);

  /// Computes mean and standard deviation of stereo audio.
  static void computeMeanStd(const float* inL, const float* inR, std::int64_t numFrames, float& mean, float& stdDev);

 private:
  DemucsConfig config_;
  std::shared_ptr<ai::IModelSession> session_;
  ChunkInferer inferer_;
};

}  // namespace zyron::stems
