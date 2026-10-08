// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace zyron::analysis {

/// High-speed radix-2 Fast Fourier Transform with precomputed twiddle factors and bit-reversal.
class Fft {
 public:
  explicit Fft(std::size_t size);
  ~Fft() = default;

  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  /// Performs forward in-place complex FFT.
  void forward(std::complex<float>* data) const;

  /// Performs inverse in-place complex FFT (scaled by 1/size).
  void inverse(std::complex<float>* data) const;

  /// Performs forward FFT on real input, outputting size/2 + 1 complex bins.
  void forwardReal(const float* realIn, std::complex<float>* complexOut) const;

  /// Performs inverse FFT on size/2 + 1 complex bins back to real output (scaled).
  void inverseReal(const std::complex<float>* complexIn, float* realOut) const;

 private:
  std::size_t size_{0};
  std::vector<std::size_t> bitReversal_;
  std::vector<std::complex<float>> twiddleForward_;
  std::vector<std::complex<float>> twiddleInverse_;
};

}  // namespace zyron::analysis
