// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Features/Fft.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace zyron::analysis {

Fft::Fft(std::size_t size) : size_(size) {
  if (size == 0 || (size & (size - 1)) != 0) {
    throw std::invalid_argument("FFT size must be a positive power of 2");
  }

  // Precompute bit-reversal indices
  bitReversal_.resize(size_);
  std::size_t numBits = 0;
  while ((1ULL << numBits) < size_) {
    numBits++;
  }

  for (std::size_t i = 0; i < size_; ++i) {
    std::size_t rev = 0;
    for (std::size_t b = 0; b < numBits; ++b) {
      if ((i >> b) & 1) {
        rev |= (1ULL << (numBits - 1 - b));
      }
    }
    bitReversal_[i] = rev;
  }

  // Precompute twiddle factors
  twiddleForward_.resize(size_ / 2);
  twiddleInverse_.resize(size_ / 2);
  for (std::size_t i = 0; i < size_ / 2; ++i) {
    const float angle = -2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(size_);
    twiddleForward_[i] = std::complex<float>(std::cos(angle), std::sin(angle));
    twiddleInverse_[i] = std::complex<float>(std::cos(-angle), std::sin(-angle));
  }
}

void Fft::forward(std::complex<float>* data) const {
  // Bit-reversal permutation
  for (std::size_t i = 0; i < size_; ++i) {
    const std::size_t j = bitReversal_[i];
    if (i < j) {
      std::swap(data[i], data[j]);
    }
  }

  // Cooley-Tukey butterfly stages
  for (std::size_t len = 2; len <= size_; len <<= 1) {
    const std::size_t half = len >> 1;
    const std::size_t step = size_ / len;

    for (std::size_t i = 0; i < size_; i += len) {
      for (std::size_t j = 0; j < half; ++j) {
        const auto u = data[i + j];
        const auto v = data[i + j + half] * twiddleForward_[j * step];
        data[i + j] = u + v;
        data[i + j + half] = u - v;
      }
    }
  }
}

void Fft::inverse(std::complex<float>* data) const {
  for (std::size_t i = 0; i < size_; ++i) {
    const std::size_t j = bitReversal_[i];
    if (i < j) {
      std::swap(data[i], data[j]);
    }
  }

  for (std::size_t len = 2; len <= size_; len <<= 1) {
    const std::size_t half = len >> 1;
    const std::size_t step = size_ / len;

    for (std::size_t i = 0; i < size_; i += len) {
      for (std::size_t j = 0; j < half; ++j) {
        const auto u = data[i + j];
        const auto v = data[i + j + half] * twiddleInverse_[j * step];
        data[i + j] = u + v;
        data[i + j + half] = u - v;
      }
    }
  }

  const float invSize = 1.0f / static_cast<float>(size_);
  for (std::size_t i = 0; i < size_; ++i) {
    data[i] *= invSize;
  }
}

void Fft::forwardReal(const float* realIn, std::complex<float>* complexOut) const {
  std::vector<std::complex<float>> buffer(size_);
  for (std::size_t i = 0; i < size_; ++i) {
    buffer[i] = std::complex<float>(realIn[i], 0.0f);
  }

  forward(buffer.data());

  const std::size_t numBins = size_ / 2 + 1;
  for (std::size_t i = 0; i < numBins; ++i) {
    complexOut[i] = buffer[i];
  }
}

void Fft::inverseReal(const std::complex<float>* complexIn, float* realOut) const {
  std::vector<std::complex<float>> buffer(size_);
  const std::size_t numBins = size_ / 2 + 1;

  for (std::size_t i = 0; i < numBins; ++i) {
    buffer[i] = complexIn[i];
  }
  for (std::size_t i = numBins; i < size_; ++i) {
    buffer[i] = std::conj(buffer[size_ - i]);
  }

  inverse(buffer.data());

  for (std::size_t i = 0; i < size_; ++i) {
    realOut[i] = buffer[i].real();
  }
}

}  // namespace zyron::analysis
