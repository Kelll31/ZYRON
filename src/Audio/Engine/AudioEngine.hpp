// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "Audio/DSP/OutputStage.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/System/EngineStats.hpp"

namespace zyron::audio {

/// Owns the audio device and the realtime callback (ROADMAP P1-05). Today the callback plays the test tone; the decks,
/// mixer and FX of Phase 2 plug in here through the OutputStage.
///
/// Threads:
///  - message thread: start(), stop(), stats(), the destructor, and the actual (re)opening of devices;
///  - any thread: onAttach()/onCommand() as a CommandSink - they only flip lock-free parameters or queue work for the
///    message thread, never block;
///  - audio thread: audioDeviceIOCallbackWithContext() - no allocation, locks, I/O or logging (ARCHITECTURE section 4).
///
/// Click-free lifecycle: before the device is stopped or reopened the output is faded out (OutputStage's tone is
/// suspended and the message thread waits until the audio thread reports silence), so changing the device or quitting
/// never cuts a waveform in the middle. The wait is bounded (kFadeOutTimeoutMs) in case the device has stalled.
class AudioEngine final : public juce::AudioIODeviceCallback,
                          public core::CommandSink,
                          public core::AudioEngineStatsSource {
 public:
  /// Upper bound for waiting on a fade-out: about two time constants longer than the fade needs (-80 dB at 5 ms).
  static constexpr juce::uint32 kFadeOutTimeoutMs = 120;

  AudioEngine();
  ~AudioEngine() override;

  /// Opens the requested output on the default or named device. A failure is not fatal: the engine stays up without a
  /// device and stats().lastError says why. Message thread.
  void start(const core::AudioOutputSettings& requested);
  /// Fades out, then stops the callback and closes the device. Message thread. Safe to call twice.
  void stop();

  // core::CommandSink
  void onAttach(const core::AppState& current) noexcept override;
  void onCommand(const core::Command& command, const core::CommandOrigin& origin) noexcept override;

  // core::AudioEngineStatsSource (message thread)
  [[nodiscard]] core::AudioEngineStats stats() const override;

  // juce::AudioIODeviceCallback
  void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                        float* const* outputChannelData, int numOutputChannels, int numSamples,
                                        const juce::AudioIODeviceCallbackContext& context) override;
  void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
  void audioDeviceStopped() override;
  void audioDeviceError(const juce::String& errorMessage) override;

 private:
  void applyOutputSettings(const core::AudioOutputSettings& requested);  // message thread
  void applyTestTone(const core::TestToneState& tone) noexcept;
  void fadeOutAndWait();  // message thread: suspends the output and waits (bounded) for the audio thread's silence
  [[nodiscard]] juce::String defaultOutputDeviceName() const;
  /// Queues `work` for the message thread unless the engine is being destroyed. Returns false (and counts the loss)
  /// when it could not be queued. Never throws.
  [[nodiscard]] bool postToMessageThread(std::function<void()> work) noexcept;

  juce::AudioDeviceManager deviceManager_;
  OutputStage stage_;
  std::atomic<std::uint64_t> callbackCount_{0};
  std::atomic<std::uint64_t> droppedRequests_{0};  // work that could not be queued for the message thread
  std::string lastError_;                          // message thread only
  std::shared_ptr<std::atomic<bool>> alive_;       // queued message-thread work checks this before touching `this`

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "the audio thread's counter must be lock-free");

}  // namespace zyron::audio
