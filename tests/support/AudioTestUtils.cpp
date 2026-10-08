// SPDX-License-Identifier: AGPL-3.0-only
#include "support/AudioTestUtils.hpp"

#include <cmath>
#include <limits>
#include <random>

namespace zyron::test {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

std::vector<float> generateSine(double sampleRate, double frequencyHz, double durationSec, float amplitude,
                                double initialPhaseRad) {
  const std::size_t numSamples = static_cast<std::size_t>(std::max(0.0, sampleRate * durationSec));
  std::vector<float> buffer(numSamples, 0.0F);
  const double phaseIncrement = 2.0 * kPi * frequencyHz / sampleRate;

  double phase = initialPhaseRad;
  for (std::size_t i = 0; i < numSamples; ++i) {
    buffer[i] = static_cast<float>(static_cast<double>(amplitude) * std::sin(phase));
    phase += phaseIncrement;
    if (phase >= 2.0 * kPi) {
      phase -= 2.0 * kPi;
    }
  }
  return buffer;
}

std::vector<float> generateImpulse(int lengthSamples, int impulsePosition, float amplitude) {
  if (lengthSamples <= 0) {
    return {};
  }
  std::vector<float> buffer(static_cast<std::size_t>(lengthSamples), 0.0F);
  if (impulsePosition >= 0 && impulsePosition < lengthSamples) {
    buffer[static_cast<std::size_t>(impulsePosition)] = amplitude;
  }
  return buffer;
}

std::vector<float> generateNoise(int lengthSamples, float amplitude, std::uint32_t seed) {
  if (lengthSamples <= 0) {
    return {};
  }
  std::vector<float> buffer(static_cast<std::size_t>(lengthSamples), 0.0F);
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-amplitude, amplitude);
  for (float& sample : buffer) {
    sample = dist(rng);
  }
  return buffer;
}

std::vector<float> generateSweep(double sampleRate, double startFreqHz, double endFreqHz, double durationSec,
                                 float amplitude) {
  const std::size_t numSamples = static_cast<std::size_t>(std::max(0.0, sampleRate * durationSec));
  std::vector<float> buffer(numSamples, 0.0F);
  if (numSamples == 0 || durationSec <= 0.0) {
    return buffer;
  }

  const double f0 = startFreqHz;
  const double f1 = endFreqHz;
  const double T = durationSec;

  for (std::size_t i = 0; i < numSamples; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    const double phase = 2.0 * kPi * (f0 * t + ((f1 - f0) / (2.0 * T)) * t * t);
    buffer[i] = static_cast<float>(static_cast<double>(amplitude) * std::sin(phase));
  }
  return buffer;
}

std::vector<float> generateClickTrack(double sampleRate, double bpm, double durationSec, double offsetSec,
                                      double clickDurationSec, double clickFreqHz) {
  const std::size_t numSamples = static_cast<std::size_t>(std::max(0.0, sampleRate * durationSec));
  std::vector<float> buffer(numSamples, 0.0F);
  if (numSamples == 0 || bpm <= 0.0) {
    return buffer;
  }

  const double beatIntervalSec = 60.0 / bpm;
  const int clickLengthSamples = static_cast<int>(sampleRate * clickDurationSec);

  for (double beatTime = offsetSec; beatTime < durationSec; beatTime += beatIntervalSec) {
    if (beatTime < 0.0) {
      continue;
    }
    const int startIdx = static_cast<int>(beatTime * sampleRate);
    for (int j = 0; j < clickLengthSamples && (startIdx + j) < static_cast<int>(numSamples); ++j) {
      const double window = std::sin(kPi * static_cast<double>(j) / static_cast<double>(clickLengthSamples));
      const double carrier = std::sin(2.0 * kPi * clickFreqHz * static_cast<double>(j) / sampleRate);
      buffer[static_cast<std::size_t>(startIdx + j)] += static_cast<float>(window * carrier);
    }
  }

  return buffer;
}

std::vector<float> generateDc(int lengthSamples, float level) {
  if (lengthSamples <= 0) {
    return {};
  }
  return std::vector<float>(static_cast<std::size_t>(lengthSamples), level);
}

bool isFinite(const std::vector<float>& buffer) noexcept {
  for (const float sample : buffer) {
    if (!std::isfinite(sample)) {
      return false;
    }
  }
  return true;
}

bool isWithinClip(const std::vector<float>& buffer, float ceiling) noexcept {
  for (const float sample : buffer) {
    if (std::abs(sample) > ceiling) {
      return false;
    }
  }
  return true;
}

float maxAbsolute(const std::vector<float>& buffer, std::size_t startIndex) noexcept {
  float maxVal = 0.0F;
  for (std::size_t i = startIndex; i < buffer.size(); ++i) {
    maxVal = std::max(maxVal, std::abs(buffer[i]));
  }
  return maxVal;
}

float nullTestMaxDiff(const std::vector<float>& a, const std::vector<float>& b) noexcept {
  if (a.size() != b.size()) {
    return std::numeric_limits<float>::infinity();
  }
  float maxDiff = 0.0F;
  for (std::size_t i = 0; i < a.size(); ++i) {
    maxDiff = std::max(maxDiff, std::abs(a[i] - b[i]));
  }
  return maxDiff;
}

CrossCorrelationResult crossCorrelation(const std::vector<float>& a, const std::vector<float>& b, int maxLag) {
  if (a.empty() || b.empty() || a.size() != b.size() || maxLag < 0) {
    return {0, 0.0F};
  }

  double energyA = 0.0;
  double energyB = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    energyA += static_cast<double>(a[i]) * static_cast<double>(a[i]);
    energyB += static_cast<double>(b[i]) * static_cast<double>(b[i]);
  }

  const double normFactor = std::sqrt(energyA * energyB);
  if (normFactor <= 1e-12) {
    return {0, 0.0F};
  }

  const int n = static_cast<int>(a.size());
  int bestLag = 0;
  double maxCorr = -1.0;

  for (int lag = -maxLag; lag <= maxLag; ++lag) {
    double sum = 0.0;
    const int startIdx = std::max(0, -lag);
    const int endIdx = std::min(n, n - lag);

    for (int i = startIdx; i < endIdx; ++i) {
      sum += static_cast<double>(a[static_cast<std::size_t>(i)]) *
             static_cast<double>(b[static_cast<std::size_t>(i + lag)]);
    }

    const double corr = sum / normFactor;
    if (corr > maxCorr) {
      maxCorr = corr;
      bestLag = lag;
    }
  }

  return {bestLag, static_cast<float>(maxCorr)};
}

float measureMagnitudeAt(const std::vector<float>& buffer, double sampleRate, double targetFreqHz) {
  if (buffer.empty() || sampleRate <= 0.0) {
    return 0.0F;
  }

  const double omega = 2.0 * kPi * targetFreqHz / sampleRate;
  const double coeff = 2.0 * std::cos(omega);

  double s0 = 0.0;
  double s1 = 0.0;
  double s2 = 0.0;

  for (const float x : buffer) {
    s0 = static_cast<double>(x) + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }

  const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  const double mag = (2.0 * std::sqrt(std::max(0.0, power))) / static_cast<double>(buffer.size());
  return static_cast<float>(mag);
}

const std::vector<int>& standardTestBlockSizes() {
  static const std::vector<int> kSizes = {32, 37, 64, 128, 256, 480, 512, 1024, 2048};
  return kSizes;
}

}  // namespace zyron::test
