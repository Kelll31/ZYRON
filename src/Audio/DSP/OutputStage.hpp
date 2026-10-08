// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Audio/DSP/TestToneGenerator.hpp"

namespace zyron::audio {

/// What the audio callback hands its output buffers to. Today it renders the test tone to every channel; the deck and
/// mixer graph of ROADMAP Phase 2 replaces the body. JUCE-free so the buffer handling (channel fan-out, null channels,
/// stale data) is tested without a device.
class OutputStage {
 public:
  /// Control thread, on every device (re)start.
  void prepare(double sampleRate) noexcept { tone_.prepare(sampleRate); }

  [[nodiscard]] TestToneGenerator& tone() noexcept { return tone_; }
  [[nodiscard]] const TestToneGenerator& tone() const noexcept { return tone_; }

  /// Audio thread. Overwrites every non-null channel of `outputs` (`numChannels` pointers to `numSamples` floats).
  /// Never leaves a channel holding old data: the audio API may hand out buffers that still contain the previous block.
  void render(float* const* outputs, int numChannels, int numSamples) noexcept;

 private:
  TestToneGenerator tone_;
};

}  // namespace zyron::audio
