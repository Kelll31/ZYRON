// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>

namespace zyron::audio {

/// Stereo time-stretch / pitch-shift engine behind a small interface (ADR-0005, ADR-0018). The mixer and the decks know
/// nothing about the library underneath (Signalsmith Stretch, MIT, header-only); it is confined to TimeStretcher.cpp.
///
/// Streaming model: every process() call consumes `inFrames` source frames and produces `outFrames` output frames; the
/// ratio inFrames / outFrames is the tempo (1.06 = 6 % faster) and the pitch is NOT changed by it. setTranspose() moves
/// the pitch independently. The engine has a latency (see inputLatency / outputLatency): after prime() the first output
/// frame is exactly the first frame handed to prime(), and the source then has to continue where prime()'s input ended.
///
/// prepare() allocates; everything else is allocation-free, lock-free and noexcept (audio thread).
class TimeStretcher {
 public:
  TimeStretcher();
  ~TimeStretcher();

  TimeStretcher(const TimeStretcher&) = delete;
  TimeStretcher& operator=(const TimeStretcher&) = delete;

  /// Non-realtime. Sizes every internal buffer for `sampleRate`; `maxInputFramesPerCall` bounds the `inFrames` of
  /// process() (and the prime length for ratios up to kMaxRatio).
  void prepare(double sampleRate, int maxInputFramesPerCall);

  /// Forgets everything that was fed in (silence in, silence out).
  void reset() noexcept;

  static constexpr double kMinRatio = 0.25;
  static constexpr double kMaxRatio = 4.0;

  /// Source frames the engine reads ahead of what it is currently outputting, at tempo ratio 1 / `ratio`.
  [[nodiscard]] int inputLatency() const noexcept;
  [[nodiscard]] int outputLatency() const noexcept;
  /// Number of source frames prime() wants for tempo ratio `ratio` (clamped to kMinRatio..kMaxRatio).
  [[nodiscard]] int primeLength(double ratio) const noexcept;
  /// Largest primeLength() over the supported ratios: the size of the buffer a caller must provide.
  [[nodiscard]] int maxPrimeLength() const noexcept;

  /// Restarts the engine at a new source position without a gap: `source` holds primeLength(ratio) frames starting at
  /// the position that must sound first. Output starts exactly there; continue feeding the source right after them.
  void prime(const float* const* source, int frames, double ratio) noexcept;

  /// Pitch multiplier (1 = unchanged, 2 = one octave up), applied to the next processed frame.
  void setTranspose(float factor) noexcept;

  void process(const float* const* source, int inFrames, float* const* out, int outFrames) noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace zyron::audio
