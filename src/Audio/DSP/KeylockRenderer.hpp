// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <vector>

#include "Audio/DSP/TimeStretcher.hpp"

namespace zyron::audio {

/// Turns a TimeStretcher into a frame-by-frame player for a deck (keylock / key shift, ADR-0018).
///
/// The deck owns the position; this class owns the stretcher, the read-ahead and a small output FIFO:
///  - The source (master track or the mix of the stems) is read through a `SourceReader` at the *nominal* tempo and
///    already at the device rate, `ratio` source frames being consumed per output frame.
///  - The stretcher always runs on fixed chunks of kChunk output frames, whatever the device block size is, so the
///    rendered audio does not depend on the block size.
///  - prime(position) restarts it at a source position (play, seek, loop wrap, switching back from varispeed). From then
///    on the n-th output frame sounds the content at position + n * ratio: the stretcher latency is compensated by
///    reading it ahead (feedPosition() runs inputLatency + ratio * outputLatency source frames ahead of what is heard).
class KeylockRenderer {
 public:
  static constexpr int kChunk = 64;

  /// Fills `left` / `right` with `frames` source frames taken at track positions position, position + step, ...
  /// (frames of the track, fractional; samples outside the track are silence). Realtime safe.
  using SourceReader = void (*)(void* context, double position, double step, int frames, float* left,
                                float* right) noexcept;

  KeylockRenderer() = default;

  /// Non-realtime: allocates the buffers.
  void prepare(double sampleRate);

  /// Next frame must be preceded by prime(): call when the playhead jumped or playback restarts.
  void invalidate() noexcept { primed_ = false; }
  [[nodiscard]] bool primed() const noexcept { return primed_; }

  /// Pitch offset in semitones relative to the source; changes are smoothed (about 15 ms) so they never click.
  void setTargetSemitones(float semitones) noexcept { targetSemitones_ = semitones; }

  /// Restarts at `headFrame` (a track frame); `srcStep` track frames per source frame, `ratio` source frames per output
  /// frame. The pitch jumps to its target at once (there is no sound to be continuous with).
  void prime(SourceReader reader, void* context, double headFrame, double srcStep, double ratio) noexcept;

  /// One output frame. Must be primed.
  void nextFrame(SourceReader reader, void* context, double srcStep, double ratio, float& left, float& right) noexcept;

  /// Source frames the stretcher reads ahead of the audible position at tempo ratio `ratio`.
  [[nodiscard]] double latencySourceFrames(double ratio) const noexcept;
  [[nodiscard]] double feedPosition() const noexcept { return feed_; }
  /// The pitch offset the stretcher is applying right now (it glides to the target).
  [[nodiscard]] float currentSemitones() const noexcept { return currentSemitones_; }
  [[nodiscard]] TimeStretcher& stretcher() noexcept { return stretcher_; }

 private:
  void produceChunk(SourceReader reader, void* context, double srcStep, double ratio) noexcept;
  void applyTranspose(bool immediate) noexcept;

  TimeStretcher stretcher_;
  bool primed_{false};
  double feed_{0.0};  // track frame of the next source frame to hand to the stretcher
  double inFrac_{0.0};
  int fifoPos_{kChunk};
  std::array<float, kChunk> fifoL_{};
  std::array<float, kChunk> fifoR_{};

  std::vector<float> inL_;  // source buffers: primeLength for the pre-roll, a chunk's worth in steady state
  std::vector<float> inR_;
  int maxIn_{0};

  float targetSemitones_{0.0F};
  float currentSemitones_{0.0F};
  float appliedSemitones_{1.0e9F};
  float transposeCoeff_{0.05F};
};

}  // namespace zyron::audio
