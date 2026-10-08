// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "Analysis/Features/Cqt.hpp"
#include "Analysis/Features/Fft.hpp"
#include "Analysis/Features/MelFilterbank.hpp"
#include "Analysis/Features/Resampler.hpp"
#include "Analysis/Features/Stft.hpp"

using namespace zyron::analysis;

namespace {

std::vector<float> generateSine(float freq, float sampleRate, std::size_t numSamples, float amp = 0.8f) {
  std::vector<float> signal(numSamples);
  const float phaseInc = 2.0f * std::numbers::pi_v<float> * freq / sampleRate;
  float phase = 0.0f;
  for (std::size_t i = 0; i < numSamples; ++i) {
    signal[i] = amp * std::sin(phase);
    phase += phaseInc;
    if (phase >= 2.0f * std::numbers::pi_v<float>) {
      phase -= 2.0f * std::numbers::pi_v<float>;
    }
  }
  return signal;
}

}  // namespace

TEST_CASE("Fft forward, inverse, and roundtrip precision", "[analysis][features][fft]") {
  constexpr std::size_t kSize = 1024;
  Fft fft(kSize);

  SECTION("delta impulse produces flat unit spectrum") {
    std::vector<float> delta(kSize, 0.0f);
    delta[0] = 1.0f;

    std::vector<std::complex<float>> bins(kSize / 2 + 1);
    fft.forwardReal(delta.data(), bins.data());

    for (const auto& b : bins) {
      CHECK_THAT(std::abs(b), Catch::Matchers::WithinRel(1.0f, 1e-4f));
    }
  }

  SECTION("roundtrip forward and inverse reconstructs arbitrary signal") {
    auto input = generateSine(440.0f, 44100.0f, kSize, 0.7f);
    std::vector<std::complex<float>> bins(kSize / 2 + 1);
    std::vector<float> reconstructed(kSize);

    fft.forwardReal(input.data(), bins.data());
    fft.inverseReal(bins.data(), reconstructed.data());

    for (std::size_t i = 0; i < kSize; ++i) {
      CHECK_THAT(reconstructed[i], Catch::Matchers::WithinAbs(input[i], 1e-4f));
    }
  }
}

TEST_CASE("Stft and iStft overlap-add reconstruction", "[analysis][features][stft]") {
  StftConfig cfg;
  cfg.nFft = 1024;
  cfg.hopLength = 256;  // exact 75% overlap for perfect reconstruction
  cfg.windowType = WindowType::HannPeriodic;
  cfg.center = true;
  cfg.scaleBySqrtN = false;
  Stft stft(cfg);

  constexpr std::size_t kLength = 4096;
  auto original = generateSine(220.0f, 44100.0f, kLength, 0.8f);

  auto complexFrames = stft.processComplex(original.data(), kLength);
  REQUIRE_FALSE(complexFrames.empty());

  auto reconstructed = stft.processInverse(complexFrames, kLength);
  REQUIRE(reconstructed.size() == kLength);

  // Check middle section away from filter edge boundaries
  for (std::size_t i = cfg.nFft; i < kLength - cfg.nFft; ++i) {
    CHECK_THAT(reconstructed[i], Catch::Matchers::WithinAbs(original[i], 0.01f));
  }
}

TEST_CASE("MelFilterbank Slaney scaling and mel-spectrogram extraction", "[analysis][features][mel]") {
  SECTION("Slaney mel <-> Hz roundtrip conversion") {
    CHECK_THAT(MelFilterbank::hzToMel(0.0f), Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    CHECK_THAT(MelFilterbank::hzToMel(1000.0f), Catch::Matchers::WithinRel(15.0f, 1e-3f));

    for (float f : {50.0f, 250.0f, 1000.0f, 2500.0f, 8000.0f, 11025.0f}) {
      const float mel = MelFilterbank::hzToMel(f);
      const float roundtripHz = MelFilterbank::melToHz(mel);
      CHECK_THAT(roundtripHz, Catch::Matchers::WithinRel(f, 1e-3f));
    }
  }

  SECTION("computes 128-band Slaney mel spectrogram for Beat This!") {
    MelFilterbank fb;
    CHECK(fb.nMels() == 128);
    CHECK(fb.numBins() == 513);
    CHECK(fb.filterbank().size() == 128);
    CHECK(fb.filterbank()[0].size() == 513);

    // 1 second of 22050 Hz audio
    auto bassSignal = generateSine(100.0f, 22050.0f, 22050, 0.8f);
    auto melSpectrogram = fb.computeMelSpectrogram(bassSignal.data(), bassSignal.size());

    REQUIRE_FALSE(melSpectrogram.empty());
    CHECK(melSpectrogram[0].size() == 128);

    // 100 Hz should activate low mel bins (indices 0..10) significantly more than high bins (indices 100..127)
    const auto& frame = melSpectrogram[melSpectrogram.size() / 2];
    float lowEnergy = 0.0f;
    for (std::size_t i = 0; i < 10; ++i) lowEnergy += frame[i];
    float highEnergy = 0.0f;
    for (std::size_t i = 100; i < 128; ++i) highEnergy += frame[i];

    CHECK(lowEnergy > highEnergy * 5.0f);
  }
}

TEST_CASE("AudioResampler sinc interpolation", "[analysis][features][resampler]") {
  constexpr std::size_t kInputSamples = 44100;
  auto input44 = generateSine(440.0f, 44100.0f, kInputSamples, 0.8f);

  SECTION("resamples from 44.1 kHz to 22.05 kHz (factor of 2 downsampling)") {
    AudioResampler resampler(44100, 22050);
    auto out22 = resampler.process(input44.data(), input44.size());

    REQUIRE(out22.size() == 22050);

    // Check steady amplitude preservation in central region
    float maxAmp = 0.0f;
    for (std::size_t i = 100; i < out22.size() - 100; ++i) {
      maxAmp = std::max(maxAmp, std::abs(out22[i]));
    }
    CHECK_THAT(maxAmp, Catch::Matchers::WithinRel(0.8f, 0.05f));
  }

  SECTION("resamples from 48 kHz to 22.05 kHz") {
    auto input48 = generateSine(440.0f, 48000.0f, 48000, 0.8f);
    AudioResampler resampler(48000, 22050);
    auto out22 = resampler.process(input48.data(), input48.size());

    REQUIRE(out22.size() == 22050);
  }
}

TEST_CASE("ConstantQTransform musical harmonic analysis", "[analysis][features][cqt]") {
  ConstantQTransform cqt;
  CHECK(cqt.numBins() == 144);

  // 1 second of 22050 Hz audio playing A4 (440 Hz)
  auto a4Signal = generateSine(440.0f, 22050.0f, 22050, 0.8f);
  auto spec = cqt.processMagnitude(a4Signal.data(), a4Signal.size());

  REQUIRE_FALSE(spec.empty());
  CHECK(spec[0].size() == 144);

  // Find peak bin in middle frame
  const auto& midFrame = spec[spec.size() / 2];
  auto maxIt = std::max_element(midFrame.begin(), midFrame.end());
  const auto peakBin = static_cast<std::size_t>(std::distance(midFrame.begin(), maxIt));
  const float peakFreq = cqt.centerFrequency(peakBin);

  // Frequency of A4 is 440 Hz; peak should be very close to 440 Hz
  CHECK_THAT(peakFreq, Catch::Matchers::WithinRel(440.0f, 0.05f));
}
