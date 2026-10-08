// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>

namespace zyron::audio {

/// A click-free sine generator for checking the output path (ROADMAP P1-05). JUCE-free, so it can be tested offline.
///
/// Threading: the setters may be called from any thread and are lock-free; render() runs on the audio thread and never
/// allocates, locks, logs or throws. prepare() resets the audio-thread state: it runs on every device (re)start, and is
/// safe against a concurrent render() only because the audio layer (JUCE's AudioDeviceManager) serialises
/// audioDeviceAboutToStart with the callback. Do not call it from anywhere else while audio is running.
///
/// All control parameters (enabled, suspended, level, frequency) live in ONE 64-bit atomic word that render() reads
/// once per block, so the audio thread always sees a consistent set - never "enabled" with the previous level.
///
/// No clicks: the output level follows its target through a one-pole smoother (5 ms time constant), so switching the
/// tone on or off and changing the level never produces a step. The phase is continuous across frequency changes and
/// keeps running while the tone is off.
class TestToneGenerator {
 public:
  static constexpr float kMinFrequencyHz = 20.0F;
  static constexpr float kMaxFrequencyHz = 20000.0F;
  static constexpr float kDefaultFrequencyHz = 440.0F;
  static constexpr float kMinLevelDb = -96.0F;
  static constexpr float kMaxLevelDb = 0.0F;
  static constexpr float kDefaultLevelDb = -20.0F;
  static constexpr double kSmoothingSeconds = 0.005;
  /// Below this gain (-80 dB) a fading tone counts as gone; see isFadedOut().
  static constexpr double kFadedGain = 1.0e-4;
  /// The frequency is limited to this fraction of the sample rate so a high tone cannot alias on a low-rate device.
  static constexpr double kMaxFractionOfSampleRate = 0.45;

  TestToneGenerator() noexcept;

  /// Sets the sample rate and returns the generator to silence (gain 0, phase 0). Not for a running render().
  void prepare(double sampleRate) noexcept;

  /// The user's switch. Ramps in/out.
  void setEnabled(bool enabled) noexcept;
  [[nodiscard]] bool enabled() const noexcept;
  /// An engine-side switch that forces silence (with the same ramp) WITHOUT changing the user's settings: used to fade
  /// the tone out before the device is stopped or reopened, so those never cut a sine mid-waveform.
  void setSuspended(bool suspended) noexcept;
  /// Clamped to [kMinFrequencyHz, kMaxFrequencyHz]; a non-finite value selects kDefaultFrequencyHz.
  void setFrequencyHz(float frequencyHz) noexcept;
  /// Clamped to [kMinLevelDb, kMaxLevelDb] and rounded to 0.01 dB; a non-finite value selects kDefaultLevelDb.
  void setLevelDb(float levelDb) noexcept;

  /// Overwrites `numSamples` mono samples. Writes silence if prepare() has not been called. Audio thread only.
  void render(float* output, int numSamples) noexcept;

  /// True once the gain has dropped below kFadedGain with a zero target. Published by render() at the end of every
  /// block, so any thread can poll it (the message thread does, while stopping the device). It reads true before the
  /// first block has run.
  [[nodiscard]] bool isFadedOut() const noexcept { return fadedOut_.load(std::memory_order_acquire); }

  // Audio-thread state, for the audio thread and for tests only (plain, non-atomic reads).
  [[nodiscard]] double gain() const noexcept { return gain_; }
  /// True when the output is exactly zero and will stay so until the tone is enabled again.
  [[nodiscard]] bool isSilent() const noexcept;

 private:
  std::atomic<std::uint64_t> params_;
  std::atomic<bool> fadedOut_{true};

  double sampleRate_{0.0};
  double smoothingCoefficient_{0.0};
  double phase_{0.0};  // radians, kept in [0, 2*pi)
  double gain_{0.0};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "the control/audio handoff must be lock-free");
static_assert(std::atomic<bool>::is_always_lock_free, "the control/audio handoff must be lock-free");

}  // namespace zyron::audio
