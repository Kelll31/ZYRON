// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>

namespace zyron::audio {

/// Click-free master mute at the very end of the output path. The engine closes it before a device is stopped or
/// reopened (so music never gets cut mid-waveform) and opens it again after the device is up. The gain follows its
/// target through a one-pole smoother (5 ms time constant, like the test tone). JUCE-free, tested offline.
///
/// Threading: setSuspended()/isClosed() are lock-free and callable from any thread; prepare() only while no process()
/// runs (device start); process() on the audio thread, no allocation or locks.
class OutputGate {
 public:
  static constexpr double kSmoothingSeconds = 0.005;
  static constexpr float kClosedGain = 1.0e-4F;  // -80 dB: counts as silent

  /// Starts closed (gain 0) at the given rate; the gate then opens through the ramp unless suspended.
  void prepare(double sampleRate) noexcept;

  void setSuspended(bool suspended) noexcept { suspended_.store(suspended, std::memory_order_release); }
  [[nodiscard]] bool suspended() const noexcept { return suspended_.load(std::memory_order_acquire); }

  /// True once a suspended gate has faded below kClosedGain. Reads true before the first block has run.
  [[nodiscard]] bool isClosed() const noexcept { return closed_.load(std::memory_order_acquire); }

  /// Audio thread. Scales every non-null channel in place.
  void process(float* const* outputs, int numChannels, int numSamples) noexcept;

 private:
  std::atomic<bool> suspended_{false};
  std::atomic<bool> closed_{true};
  float gain_{0.0F};
  float coefficient_{0.005F};
};

}  // namespace zyron::audio
