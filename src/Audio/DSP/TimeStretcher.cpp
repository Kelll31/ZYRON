// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/DSP/TimeStretcher.hpp"

#include <algorithm>
#include <cmath>

// Third-party header: not held to our warning policy (the include directories are SYSTEM as well).
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "signalsmith-stretch/signalsmith-stretch.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace zyron::audio {

namespace {

/// Planar pointer pair seen as `inputs[channel][frame]`, which is what the library wants.
struct PlanarView {
  const float* const* channels;
  const float* operator[](int channel) const noexcept { return channels[channel]; }
};
struct PlanarOut {
  float* const* channels;
  float* operator[](int channel) const noexcept { return channels[channel]; }
};

constexpr long kFixedSeed = 0x5EED;  // deterministic phase randomisation: the same input renders the same output

}  // namespace

struct TimeStretcher::Impl {
  Impl() : stretch(kFixedSeed) {}
  signalsmith::stretch::SignalsmithStretch<float> stretch;
  double sampleRate{48000.0};
  int maxPrime{0};
  bool prepared{false};
};

TimeStretcher::TimeStretcher() : impl_(std::make_unique<Impl>()) {}
TimeStretcher::~TimeStretcher() = default;

void TimeStretcher::prepare(double sampleRate, int maxInputFramesPerCall) {
  impl_->sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
  // The library's "cheaper" preset (100 ms block, 40 ms hop) measured ~0.7 % of a core per stereo deck at 48 kHz and
  // ~0.6 ms to prime, against ~1.1 % and ~0.9 ms for its default; the difference is inaudible on full mixes.
  // splitComputation spreads each spectral frame over the whole hop: a flat CPU curve instead of a spike every 40 ms,
  // at the price of one extra hop of latency (which the deck compensates exactly).
  impl_->stretch.presetCheaper(2, static_cast<float>(impl_->sampleRate), true);
  impl_->stretch.reset();
  // The library resizes its scratch up to (block + interval) frames, and the pre-roll needs inputLatency + ratio *
  // outputLatency; reserve for the worst case so nothing grows on the audio thread.
  impl_->maxPrime = inputLatency() + static_cast<int>(std::ceil(kMaxRatio * outputLatency())) + 16;
  impl_->maxPrime = std::max(impl_->maxPrime, maxInputFramesPerCall);
  impl_->prepared = true;
}

// RT
void TimeStretcher::reset() noexcept {
  impl_->stretch.reset();
}

int TimeStretcher::inputLatency() const noexcept {
  return impl_->stretch.inputLatency();
}

int TimeStretcher::outputLatency() const noexcept {
  return impl_->stretch.outputLatency();
}

int TimeStretcher::primeLength(double ratio) const noexcept {
  ratio = std::clamp(ratio, kMinRatio, kMaxRatio);
  return impl_->stretch.outputSeekLength(static_cast<float>(ratio));
}

int TimeStretcher::maxPrimeLength() const noexcept {
  return impl_->maxPrime;
}

// RT
void TimeStretcher::prime(const float* const* source, int frames, double /*ratio*/) noexcept {
  if (!impl_->prepared || source == nullptr || frames <= 0) {
    return;
  }
  impl_->stretch.outputSeek(PlanarView{source}, frames);
}

// RT
void TimeStretcher::setTranspose(float factor) noexcept {
  impl_->stretch.setTransposeFactor(std::clamp(factor, 0.25F, 4.0F));
}

void TimeStretcher::process(const float* const* source, int inFrames, float* const* out, int outFrames) noexcept {
  if (!impl_->prepared) {
    return;
  }
  impl_->stretch.process(PlanarView{source}, inFrames, PlanarOut{out}, outFrames);
}

}  // namespace zyron::audio
