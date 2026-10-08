// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "Audio/DSP/OutputGate.hpp"
#include "Audio/DSP/OutputStage.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "Audio/Deck/TrackLoader.hpp"
#include "Audio/Engine/AudioGraph.hpp"
#include "Core/Audio/EngineView.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/System/EngineStats.hpp"

namespace zyron::audio {

/// Owns the audio device, the realtime callback and the 4-deck AudioGraph (ROADMAP P1-05, P2-05, P11). Every command
/// the bus accepts either reaches the graph through the lock-free CommandBridge or is handled here (LoadTrack,
/// UnloadTrack, device and test-tone commands); nothing is dropped silently. The test tone is mixed on top of the
/// master output.
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
                          public core::AudioEngineStatsSource,
                          public core::ILiveEngineSource,
                          public core::IDeckLoadSource {
 public:
  /// What the engine needs from the rest of the application, injected by the composition root so Audio never depends
  /// on Library or Analysis.
  /// A beat grid as the library knows it.
  struct GridInfo {
    double bpm{0.0};
    double firstBeatSec{0.0};  // any beat of the grid, in seconds from the track start
  };

  struct Services {
    /// The beat grid of a library track, if it has been analysed. Called on the command thread. Optional.
    std::function<std::optional<GridInfo>(core::TrackId)> gridFor;
    /// Maps a library track id to its file. Called on the command thread; must be quick and thread-safe.
    std::function<std::optional<std::filesystem::path>(core::TrackId)> resolvePath;
    /// Builds display peaks for a freshly decoded track. Called on the loader thread. Optional.
    std::function<std::shared_ptr<const core::WaveformData>(const TrackBuffer&)> makeWaveform;
    /// Starts the neural stem separation of a loaded track (it runs elsewhere; results come back through
    /// attachStems / setStemStatus). Called on the command thread, so it must only queue the work.
    std::function<void(core::DeckId, core::TrackId)> separateStems;
    /// Called on the loader thread when a track has finished loading onto a deck (e.g. to attach cached stems).
    std::function<void(core::DeckId, core::TrackId)> trackReady;
    /// Starts or stops recording the master output. Called on the message thread. Returns an error text, or empty.
    std::function<std::string(bool enabled)> setRecording;
  };

  /// Upper bound for waiting on a fade-out: about two time constants longer than the fade needs (-80 dB at 5 ms).
  static constexpr juce::uint32 kFadeOutTimeoutMs = 120;

  AudioEngine();
  explicit AudioEngine(Services services);
  ~AudioEngine() override;

  /// Opens the requested output on the default or named device. A failure is not fatal: the engine stays up without a
  /// device and stats().lastError says why. Message thread.
  void start(const core::AudioOutputSettings& requested);
  /// Fades out, then stops the callback and closes the device. Message thread. Safe to call twice.
  void stop();

  // core::CommandSink
  void onAttach(const core::AppState& current) noexcept override;
  void onCommand(const core::Command& command, const core::CommandOrigin& origin) noexcept override;

  /// Where the master output is copied to while recording (null = nowhere). Any thread; the tap must outlive the
  /// engine or be cleared first. The tap is called on the audio thread and must be realtime-safe.
  void setMasterTap(core::IAudioTap* tap) noexcept { graph_->setMasterTap(tap); }

  /// The track buffer on a deck (null when empty). Any thread; the buffer stays valid while the pointer is held.
  [[nodiscard]] std::shared_ptr<const TrackBuffer> deckBuffer(core::DeckId deck) const {
    return graph_->deck(deck).currentTrack();
  }
  /// Records the progress of a stem separation for `track`; ignored when the deck has since loaded another track.
  void setStemStatus(core::DeckId deck, core::TrackId track, core::StemPhase phase, float progress,
                     std::string message);
  /// Switches the deck to separated stems (vocals, drums, bass, other: same length and rate as the track). Returns
  /// false when the deck no longer holds `track` or its buffer is not `reference` (the one the stems were made from).
  /// Any non-realtime thread.
  bool attachStems(core::DeckId deck, core::TrackId track, const std::shared_ptr<const TrackBuffer>& reference,
                   std::array<std::shared_ptr<const TrackBuffer>, core::kStemKindCount> stems);

  // core::AudioEngineStatsSource (message thread)
  [[nodiscard]] core::AudioEngineStats stats() const override;

  // core::ILiveEngineSource (UI thread)
  [[nodiscard]] core::LiveEngineState liveState() override;

  // core::IDeckLoadSource (any thread)
  [[nodiscard]] core::DeckLoadStatus loadStatus(core::DeckId deck) const override;

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
  void startLoad(const core::LoadTrack& command) noexcept;
  void startUnload(const core::UnloadTrack& command) noexcept;
  void performSync(core::DeckId target) noexcept;
  void postNotice(std::string text);
  void finishLoad(core::DeckId deck, std::uint64_t requestId, core::TrackId track, const TrackLoadResult& result);
  void setLoadStatus(core::DeckId deck, core::DeckLoadStatus status);
  void mixTestTone(float* const* outputs, int numChannels, int numSamples) noexcept;
  void fadeOutAndWait();  // message thread: suspends the output and waits (bounded) for the audio thread's silence
  [[nodiscard]] juce::String defaultOutputDeviceName() const;
  /// Queues `work` for the message thread unless the engine is being destroyed. Returns false (and counts the loss)
  /// when it could not be queued. Never throws.
  [[nodiscard]] bool postToMessageThread(std::function<void()> work) noexcept;

  static constexpr int kToneBlock = AudioGraph::kMaxBlockSize;

  Services services_;
  juce::AudioDeviceManager deviceManager_;
  OutputStage stage_;  // owns the test-tone generator
  OutputGate gate_;
  std::unique_ptr<AudioGraph> graph_;  // large (scratch buffers): heap
  std::unique_ptr<TrackLoader> loader_;  // declared after graph_: destroyed (and joined) first
  std::array<float, kToneBlock> toneScratch_{};

  mutable std::mutex loadMutex_;
  std::array<core::DeckLoadStatus, core::kDeckCount> loadStatus_{};
  std::array<std::uint64_t, core::kDeckCount> latestRequest_{};
  std::uint64_t statusGeneration_{0};
  std::string notice_;
  std::uint64_t noticeSerial_{0};

  std::atomic<std::uint64_t> callbackCount_{0};
  std::atomic<std::uint64_t> droppedRequests_{0};  // work that could not be queued for the message thread
  std::string lastError_;                          // message thread only
  std::shared_ptr<std::atomic<bool>> alive_;       // queued message-thread work checks this before touching `this`

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "the audio thread's counter must be lock-free");

}  // namespace zyron::audio
