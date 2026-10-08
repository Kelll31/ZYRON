// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "Audio/DSP/KeylockRenderer.hpp"
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

  using StemBuffers = std::array<std::shared_ptr<const TrackBuffer>, core::kStemKindCount>;

  /// What a call took off the deck. The audio thread may still be inside a block that reads these buffers, so the
  /// caller must hand them to the deferred-retirement list instead of letting them die. The swap and the capture
  /// happen under one lock: no other thread can slip in between.
  struct Released {
    std::shared_ptr<const TrackBuffer> track;
    StemBuffers stems;
  };

  /// Loads a new track buffer (the stems of the old track go with it). The player takes shared ownership; the audio
  /// thread accesses the buffer via lock-free pointer exchange. Non-realtime thread.
  Released loadTrack(std::shared_ptr<const TrackBuffer> track) noexcept;

  /// Unloads the current track and its stems. Non-realtime thread.
  Released unloadTrack() noexcept;

  /// Loads 4-stem tracks (Vocals, Drums, Bass, Other); returns the stems they replace. Non-realtime thread.
  StemBuffers loadStems(StemBuffers stems) noexcept;

  /// The stems currently loaded (empty pointers when none). Non-realtime thread; keep a copy before replacing or
  /// unloading so the buffers can be retired later instead of freed under the audio thread.
  [[nodiscard]] std::array<std::shared_ptr<const TrackBuffer>, core::kStemKindCount> currentStems() const noexcept {
    std::lock_guard<std::mutex> lock(controlMutex_);
    return retainedStems_;
  }

  /// Loads stems only if `expectedTrack` is still the track on the deck: stems separated from a track that has been
  /// replaced meanwhile must never be played over the new one. False (nothing changed) when it is not.
  /// On success returns the stems it replaced (to retire); the check and the swap are one atomic step.
  std::optional<StemBuffers> loadStemsFor(const TrackBuffer* expectedTrack, StemBuffers stems) noexcept;

  /// Unloads stems; returns them for retirement. Non-realtime thread.
  StemBuffers unloadStems() noexcept;

  /// Starts playback with click-free fade-in. Can be called from any thread.
  void play() noexcept;

  /// Stops playback with click-free fade-out. Can be called from any thread.
  void pause() noexcept;

  /// DJ Cue: if playing, jumps back to cue frame and pauses; if paused, sets cue to current frame.
  void cue() noexcept;

  /// Audio thread (from the command queue): performs a core::ScratchPattern for `beats` beats of `beatSeconds` each,
  /// then carries on where the record would have been without it; a backspin stops the deck. Needs a playing deck.
  void startScratch(int pattern, double beats, double beatSeconds) noexcept;

  /// Audio thread (from the command queue): moves the speed to `target` linearly over `seconds`, a little every
  /// block. Any later setPlaybackSpeed (a sync, the pitch fader) cancels it.
  void glideSpeed(double target, double seconds) noexcept;

  /// The buffer the audio thread plays (identity only, never dereferenced by callers). Any thread.
  [[nodiscard]] const TrackBuffer* activeTrack() const noexcept { return activeBuffer_.load(std::memory_order_acquire); }

  /// Moves playhead to a specific frame.
  void seek(std::int64_t frame) noexcept;

  /// Moves playhead to a specific time in seconds.
  void seekSeconds(double seconds) noexcept;

  /// Sets playback speed factor (1.0 = normal, 1.05 = +5%, 0.5 = half speed).
  void setPlaybackSpeed(double speed) noexcept;

  /// Keylock (master tempo, on by default): a tempo other than 1.0 keeps the pitch. Off = varispeed like a turntable.
  /// Applies to the full mix and to the stems (the stems are mixed first, then stretched: one stretcher per deck).
  /// Scratches, brakes, backspins and short loops (loop rolls) always play varispeed and resume cleanly.
  void setKeylock(bool enabled) noexcept { keylockEnabled_.store(enabled, std::memory_order_relaxed); }
  [[nodiscard]] bool keylockEnabled() const noexcept { return keylockEnabled_.load(std::memory_order_relaxed); }

  /// Key shift in semitones (-6..+6, fractions allowed), independent of the tempo. Changes glide in about 15 ms.
  void setKeyShift(float semitones) noexcept;
  [[nodiscard]] float keyShift() const noexcept { return keyShiftSemitones_.load(std::memory_order_relaxed); }

  /// Audio thread: whether this deck may restart its stretcher in the next render() (a restart costs about half a
  /// millisecond; AudioGraph hands the permission to one deck per block). A deck that is not allowed keeps playing the
  /// untouched signal and fades the stretcher in later. Defaults to true. takePrimedFlag() tells whether it was used.
  void setPrimeAllowed(bool allowed) noexcept { primeAllowed_ = allowed; }
  [[nodiscard]] bool takePrimedFlag() noexcept { return std::exchange(primedThisBlock_, false); }

  /// True while the last rendered block went through the time-stretcher (false: plain varispeed or idle).
  [[nodiscard]] bool isStretching() const noexcept { return stretching_.load(std::memory_order_relaxed); }

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
  /// Same as setLoop + setLoopActive, with the region given in seconds of the loaded track. No-op without a track.
  void setLoopSeconds(double startSeconds, double endSeconds, bool active) noexcept;
  void setLoopActive(bool active) noexcept;
  [[nodiscard]] bool isLoopActive() const noexcept;
  [[nodiscard]] std::int64_t loopStartFrame() const noexcept;
  [[nodiscard]] std::int64_t loopEndFrame() const noexcept;

 private:
  struct JumpList;
  struct BlockParams;
  /// Keylock path of render(): same per-sample logic as the varispeed loops, but the audio comes out of the stretcher.
  void renderStretched(float* const* out, int numChannels, int numSamples, const BlockParams& block, JumpList& jumps,
                       double& head) noexcept;
  void forgetStretch() noexcept;
  [[nodiscard]] bool wantsStretch(double speed, const TrackBuffer* reference) const noexcept;
  static void readSource(void* context, double position, double step, int frames, float* left, float* right) noexcept;

  double deviceSampleRate_{48000.0};

  // The retained shared_ptrs are touched by non-realtime threads only (loader, command, stem threads): one mutex
  // serialises them. The audio thread never takes it; it reads the raw atomic pointers below.
  mutable std::mutex controlMutex_;
  // Any non-audio thread that writes the playhead (load, seek, cue) bumps this; the audio thread then does not
  // overwrite the new position with the one it computed at the start of its block.
  std::atomic<std::uint32_t> positionEpoch_{0};
  std::atomic<bool> rampResetRequested_{false};  // the audio thread zeroes playRamp_ (a plain float it owns)
  std::atomic<bool> jumpPending_{false};         // the playhead jumped: smooth the step in the output

  // Audio-thread state of the click suppression after a jump (seek, loop wrap, switch to stems): the output starts at
  // the last value it had and decays to the new signal within a few milliseconds.
  float lastOutL_{0.0F};
  float lastOutR_{0.0F};
  /// Adds a decaying correction after a jump of the read position so the output has no step (audio thread only).
  void applyDeclick(float* const* out, int numChannels, int numSamples, const int* jumpsAt, int jumpCount) noexcept;

  std::vector<float> stemAmp_;  // per-sample play ramp * gain * volume of the stems path
  /// Installs stems under controlMutex_ and returns the ones replaced.
  StemBuffers installStemsLocked(StemBuffers stems) noexcept;
  /// Fades the last output value out over a few ms when the deck goes silent abruptly (cue, eject).
  void releaseTail(float* const* out, int numChannels, int numSamples) noexcept;

  /// Velocity (x normal) and gain of the running scratch for one sample; false when it has just ended. Audio thread.
  bool advanceScratch(double& velocity, float& gain) noexcept;
  struct ScratchState {
    bool active{false};
    int pattern{0};
    double pos{0.0};          // device samples since it began
    double length{0.0};       // device samples
    double beatSamples{1.0};
    double anchor{0.0};       // track frame where it began
    double gate{1.0};         // smoothed fader of the pattern
    std::uint32_t epoch{0};   // positionEpoch_ when it began: a seek since then cancels it
  };
  ScratchState scratch_{};    // audio thread only
  double scratchGateCoeff_{0.02};

  float declickL_{0.0F};
  float declickR_{0.0F};
  float declickGain_{0.0F};
  float declickDecay_{0.99F};
  bool lastStemsMode_{false};

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
  std::atomic<std::uint32_t> speedSerial_{0};  // bumped by setPlaybackSpeed: cancels a running glide
  struct Glide {
    bool active{false};
    double target{1.0};
    double perSample{0.0};
    std::uint32_t serial{0};
  };
  Glide glide_{};  // audio thread only

  // Loop control
  std::atomic<bool> loopActive_{false};
  std::atomic<std::int64_t> loopStartFrame_{0};
  std::atomic<std::int64_t> loopEndFrame_{0};

  // Keylock / key shift (controls: any thread; the rest: audio thread only)
  std::atomic<bool> keylockEnabled_{true};
  std::atomic<float> keyShiftSemitones_{0.0F};
  std::atomic<bool> stretching_{false};
  KeylockRenderer keylock_;
  bool primeAllowed_{true};
  bool primedThisBlock_{false};
  bool reprimePending_{false};  // a loop wrap waits for its turn at the prime budget; the stretcher keeps running meanwhile
  bool stretchPath_{false};            // the previous block went through the stretcher (hysteresis, declick on change)
  bool stretchExiting_{false};         // the stretcher is being left: the untouched signal fades in
  float stretchBlend_{0.0F};           // weight of the untouched signal in the output (1 = only the untouched one)
  float stretchBlendStep_{0.002F};
  std::uint32_t stretchEpoch_{0};      // positionEpoch_ the stretcher was primed under
  bool sourceStems_{false};            // readSource(): read the mix of the stems instead of the full mix
  const TrackBuffer* sourceMaster_{nullptr};
  std::array<const TrackBuffer*, core::kStemKindCount> sourceStemBufs_{};

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
