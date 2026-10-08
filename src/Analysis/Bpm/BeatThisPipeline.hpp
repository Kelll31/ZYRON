// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "Analysis/Bpm/BeatDetector.hpp"

namespace zyron::analysis {

/// Runs one 30 s window through the Beat This! network. `spect` is [kChunkFrames x kMelBins] row-major; the callback
/// fills `beat` and `downbeat` with kChunkFrames logits each and returns false when the inference failed.
using BeatThisInference = std::function<bool(const float* spect, float* beat, float* downbeat)>;

/// Beat and downbeat tracking with the Beat This! network (ISMIR 2024, ONNX export; docs/AI_MODELS.md, ADR-0010).
///
/// The network only covers the model itself; the host owns the exact feature contract and the window layout, which
/// this class implements: 22.05 kHz mono, STFT 1024 / hop 441 with a periodic Hann window and reflect padding,
/// magnitude divided by sqrt(1024), projection onto the shipped mel filterbank, log1p(1000 x); windows of 1500 frames
/// with 6-frame borders that keep the first prediction where windows overlap; peak picking on the logits (+-3 frames,
/// above 0); and a beat grid fitted through the beats (BeatDetector::buildGridFromBeats).
///
/// The inference itself is injected, so the pipeline is tested without the model and runs on any backend.
class BeatThisPipeline {
 public:
  static constexpr int kSampleRate = 22050;
  static constexpr int kMelBins = 128;
  static constexpr int kFftBins = 513;
  static constexpr int kChunkFrames = 1500;
  static constexpr int kBorderFrames = 6;
  static constexpr double kFramesPerSecond = 50.0;

  /// `melFilterbank` is the shipped mel-filterbank.bin: kFftBins x kMelBins floats, row-major.
  BeatThisPipeline(std::vector<float> melFilterbank, BeatThisInference inference);

  /// Log-mel features of 22.05 kHz mono audio: frames x kMelBins, row-major.
  [[nodiscard]] std::vector<float> logMel(const float* mono22k, std::size_t numSamples) const;

  /// Per-frame beat and downbeat logits for a whole feature matrix (windowing and aggregation). Returns false when an
  /// inference call failed.
  [[nodiscard]] bool predict(const std::vector<float>& features, std::vector<float>& beatLogits,
                             std::vector<float>& downbeatLogits) const;

  /// Full analysis of mono audio at any sample rate (arithmetic-mean downmix is the caller's job).
  [[nodiscard]] BeatDetectionResult analyze(const float* mono, std::size_t numSamples, int sampleRate,
                                            TempoPriorMode prior, std::string* error = nullptr) const;

 private:
  std::vector<float> melFilterbank_;
  BeatThisInference inference_;
};

}  // namespace zyron::analysis
