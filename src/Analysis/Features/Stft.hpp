// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "Analysis/Features/Fft.hpp"

namespace zyron::analysis {

enum class WindowType {
  HannPeriodic,   // Periodic Hann window (PyTorch default, Beat This! contract)
  HannSymmetric,  // Standard symmetric Hann window
  Hamming,
  Rectangular
};

struct StftConfig {
  std::size_t nFft{1024};
  std::size_t hopLength{441};
  WindowType windowType{WindowType::HannPeriodic};
  bool center{true};
  bool scaleBySqrtN{true};  // divide by sqrt(nFft) per Beat This! contract
};

/// Short-Time Fourier Transform and Inverse STFT (ROADMAP P3-13, docs/AI_MODELS.md).
class Stft {
 public:
  explicit Stft(StftConfig config = {});

  [[nodiscard]] const StftConfig& config() const noexcept { return config_; }
  [[nodiscard]] std::size_t numBins() const noexcept { return config_.nFft / 2 + 1; }

  /// Computes the complex STFT of a 1D audio signal.
  /// Output is [numFrames][numBins].
  [[nodiscard]] std::vector<std::vector<std::complex<float>>> processComplex(
      const float* audio, std::size_t numSamples) const;

  /// Computes the magnitude spectrogram of a 1D audio signal.
  /// Output is [numFrames][numBins].
  [[nodiscard]] std::vector<std::vector<float>> processMagnitude(
      const float* audio, std::size_t numSamples) const;

  /// Computes the power spectrogram (|X|^2) of a 1D audio signal.
  [[nodiscard]] std::vector<std::vector<float>> processPower(
      const float* audio, std::size_t numSamples) const;

  /// Computes the inverse STFT from complex frames back to time-domain audio.
  [[nodiscard]] std::vector<float> processInverse(
      const std::vector<std::vector<std::complex<float>>>& complexFrames,
      std::size_t expectedSamples = 0) const;

 private:
  void initWindow();
  [[nodiscard]] std::vector<float> padReflect(const float* audio, std::size_t numSamples) const;

  StftConfig config_;
  Fft fft_;
  std::vector<float> window_;
  float normScale_{1.0f};
};

}  // namespace zyron::analysis
