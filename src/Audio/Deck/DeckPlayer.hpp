// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Audio/DSP/StemMixer.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Sample-accurate player for a single deck (ROADMAP P2-02, P5-06, SPEC sections 9, 12, 14, 43, 46).
///
/// Features:
///  - Play / Pause / Cue / Seek
///  - Sample-accurate fractional position tracking with linear/Hermite interpolation
///  - Playback rate / speed adjustment (varispeed / tempo)
///  - Anti-click parameter smoothing (5 ms fade on start/pause/seek)
///  - Gain (dB) and Volume fader (0..1)
///  - 4-stem playback (Vocals, Drums, Bass, Other) with sample-lock synchronization
///  - Integrated StemMixer for volume/mute/solo/cue and per-stem EQ/filter
///  - Completely allocation-free, lock-free, and realtime-safe on the audio thread.
class DeckPlayer {
 public:
  DeckPlayer();
  ~DeckPlayer() = default;

  DeckPlayer(const DeckPlayer&) = delete;
  DeckPlayer& operator=(const DeckPlayer&) = delete;

  /// Sets the device sampling rate. Call before rendering. Non-realtime thread.
  void prepare(double deviceSampleRate) noexcept;

  /// Loads a new track buffer. The player takes shared ownership; the audio thread
  /// accesses the buffer via lock-free pointer exchange. Non-realtime thread.
  void loadTrack(std::shared_ptr<const TrackBuffer> track) noexcept;

  /// Unloads the current track. Non-realtime thread.
  void unloadTrack() noexcept;

  /// Loads 4-stem tracks (Vocals, Drums, Bass, Other). Non-realtime thread.
  void loadStems(std::array<std::shared_ptr<const TrackBuffer>, core::kStemKindCount> stems) noexcept;

  /// Unloads stems. Non-realtime thread.
  void unloadStems() noexcept;

  /// Starts playback with click-free fade-in. Can be called from any thread.
  void play() noexcept;

  /// Stops playback with click-free fade-out. Can be called from any thread.
  void pause() noexcept;

  /// DJ Cue: if playing, jumps back to cue frame and pauses; if paused, sets cue to current frame.
  void cue() noexcept;

  /// Moves playhead to a specific frame.
  void seek(std::int64_t frame) noexcept;

  /// Moves playhead to a specific time in seconds.
  void seekSeconds(double seconds) noexcept;

  /// Sets playback speed factor (1.0 = normal, 1.05 = +5%, 0.5 = half speed).
  void setPlaybackSpeed(double speed) noexcept;

  /// Sets deck trim gain in dB (-24 dB to +12 dB).
  void setGainDb(float gainDb) noexcept;

  /// Sets channel fader volume (0.0 to 1.0 linear).
  void setVolume(float linearVolume) noexcept;

  /// Realtime rendering callback. Renders into planar output channels.
  /// Never allocates, locks, or throws.
  void render(float* const* outputChannels, int numChannels, int numSamples) noexcept;

  /// Renders individual processed stems into separate planar buffers (SPEC section 44).
  /// Zero allocations, realtime safe.
  void renderStems(float* const* outStemLefts, float* const* outStemRights, int numSamples) noexcept;

  // Telemetry accessors
  [[nodiscard]] bool hasTrack() const noexcept;
  [[nodiscard]] std::shared_ptr<const TrackBuffer> currentTrack() const noexcept;
  [[nodiscard]] bool hasStems() const noexcept;
  [[nodiscard]] StemMixer& stemMixer() noexcept { return stemMixer_; }
  [[nodiscard]] const StemMixer& stemMixer() const noexcept { return stemMixer_; }
  [[nodiscard]] bool isPlaying() const noexcept;
  [[nodiscard]] std::int64_t currentFrame() const noexcept;
  [[nodiscard]] double currentTimeSec() const noexcept;
  [[nodiscard]] std::int64_t cueFrame() const noexcept;
  [[nodiscard]] double durationSec() const noexcept;
  [[nodiscard]] double playbackSpeed() const noexcept;

  // Loop control (§26)
  void setLoop(std::int64_t startFrame, std::int64_t endFrame) noexcept;
  void setLoopActive(bool active) noexcept;
  [[nodiscard]] bool isLoopActive() const noexcept;
  [[nodiscard]] std::int64_t loopStartFrame() const noexcept;
  [[nodiscard]] std::int64_t loopEndFrame() const noexcept;

 private:
  double deviceSampleRate_{48000.0};

  // Track buffer management
  std::shared_ptr<const TrackBuffer> retainedTrack_{nullptr};
  std::atomic<const TrackBuffer*> activeBuffer_{nullptr};

  // Stems management
  StemMixer stemMixer_;
  std::array<std::shared_ptr<const TrackBuffer>, core::kStemKindCount> retainedStems_{};
  std::array<std::atomic<const TrackBuffer*>, core::kStemKindCount> activeStemBuffers_{};
  std::atomic<bool> hasStems_{false};

  // Preallocated scratch buffers for realtime stem rendering
  std::array<std::vector<float>, core::kStemKindCount> stemScratchL_{};
  std::array<std::vector<float>, core::kStemKindCount> stemScratchR_{};
  std::vector<float> stemMixMasterL_{};
  std::vector<float> stemMixMasterR_{};

  // State flags & positions
  std::atomic<bool> playing_{false};
  std::atomic<bool> targetPlaying_{false};
  std::atomic<double> playhead_{0.0};
  std::atomic<std::int64_t> cueFrame_{0};
  std::atomic<double> speed_{1.0};

  // Loop control
  std::atomic<bool> loopActive_{false};
  std::atomic<std::int64_t> loopStartFrame_{0};
  std::atomic<std::int64_t> loopEndFrame_{0};

  // Smoothed parameters
  std::atomic<float> targetGainDb_{0.0F};
  std::atomic<float> targetVolume_{1.0F};
  float currentGainLinear_{1.0F};
  float currentVolume_{1.0F};
  float playRamp_{0.0F};  // 0.0 = silent, 1.0 = fully faded in

  // Smoothing filter coefficient
  float rampCoeff_{0.005F};
};

}  // namespace zyron::audio
