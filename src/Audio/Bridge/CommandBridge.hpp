// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "Audio/Bridge/AudioTelemetry.hpp"
#include "Audio/DSP/Mixer.hpp"
#include "Core/Commands/Command.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Fixed-size POD message sent from control/UI threads to the realtime audio thread (ARCHITECTURE section 5).
enum class RtMessageType : std::uint8_t {
  None = 0,
  DeckPlay,
  DeckPause,
  DeckCue,
  DeckSeek,
  DeckGain,
  DeckVolume,
  DeckEq,
  DeckFilter,
  DeckPlaybackSpeed,
  MixerCrossfader,
  MixerCrossfaderCurve,
  MixerChannelAssign,
  MixerMasterGain,
  MixerLimiter,
  MixerCue,
  TestTone,
  DeckStemVolume,
  DeckStemMute,
  DeckStemSolo,
  DeckStemCue,
  DeckSeekSeconds,
  DeckLoop,
  DeckScratch,
  DeckPhaseLock,
  DeckTempoGlide,
  DeckKeylock,
  DeckKeyShift,
  DeckFx,
  DeckFxTempo,
  DeckTrackTrim,
  MixerProcessing,
  MixerFxHit
};

struct RtMessage {
  RtMessageType type{RtMessageType::None};
  core::DeckId deck{core::DeckId::A};
  union {
    std::int64_t seekSample;
    double seekSeconds;
    struct {
      double startSeconds;
      double endSeconds;
      bool active;
    } loop;
    float gainDb;
    float volumeLinear;
    struct {
      core::EqBand band;
      float gainDb;
    } eq;
    float filterBipolar;
    struct {
      std::uint8_t master;
      double targetBpm;
      std::int64_t targetFirstBeat;
      int targetRate;
      double masterBpm;
      std::int64_t masterFirstBeat;
      int masterRate;
    } phaseLock;
    struct {
      double speed;
      double seconds;
    } glide;
    struct {
      std::uint8_t pattern;
      float beats;
      float beatSeconds;
    } scratch;
    double speedRatio;
    bool keylockEnabled;
    float keyShiftSemitones;
    struct {
      std::uint8_t slot;
      core::FxType type;
      bool enabled;
      bool tailAfterFader;
      float wet;
      float param;
    } fx;
    float fxBeatSeconds;
    float trackTrimDb;
    struct {
      bool glue;
      bool limiter;
    } processing;
    struct {
      core::FxHitType type;
      float level;
      double beatSeconds;
    } fxHit;
    float crossfaderPosition;
    CrossfaderCurve crossfaderCurve;
    struct {
      int channel;
      CrossfaderAssign assign;
    } channelAssign;
    float masterGainDb;
    struct {
      bool enabled;
      float ceilingDb;
    } limiter;
    struct {
      int channel;
      bool enabled;
    } cue;
    struct {
      bool enabled;
      float frequencyHz;
      float levelDb;
    } testTone;
    struct {
      core::StemKind stem;
      float volumeLinear;
      bool active;
    } stemControl;
  } data{};

  static RtMessage makePlay(core::DeckId deck) noexcept;
  static RtMessage makePause(core::DeckId deck) noexcept;
  static RtMessage makeCue(core::DeckId deck) noexcept;
  static RtMessage makeSeek(core::DeckId deck, std::int64_t sample) noexcept;
  static RtMessage makeSeekSeconds(core::DeckId deck, double seconds) noexcept;
  static RtMessage makeLoop(core::DeckId deck, double startSeconds, double endSeconds, bool active) noexcept;
  static RtMessage makeGain(core::DeckId deck, float gainDb) noexcept;
  static RtMessage makeVolume(core::DeckId deck, float linear) noexcept;
  static RtMessage makeEq(core::DeckId deck, core::EqBand band, float gainDb) noexcept;
  static RtMessage makeFilter(core::DeckId deck, float bipolar) noexcept;
  static RtMessage makeTempoGlide(core::DeckId deck, double speed, double seconds) noexcept;
  /// Start `deck` in phase with `master` on its next Play, using these grids (copied: no shared state).
  static RtMessage makePhaseLock(core::DeckId deck, core::DeckId master, double targetBpm, std::int64_t targetFirstBeat,
                                int targetRate, double masterBpm, std::int64_t masterFirstBeat, int masterRate) noexcept;
  static RtMessage makeScratch(core::DeckId deck, core::ScratchPattern pattern, double beats, double beatSeconds) noexcept;
  static RtMessage makePlaybackSpeed(core::DeckId deck, double speedRatio) noexcept;
  static RtMessage makeKeylock(core::DeckId deck, bool enabled) noexcept;
  static RtMessage makeKeyShift(core::DeckId deck, float semitones) noexcept;
  static RtMessage makeFx(core::DeckId deck, int slot, core::FxType type, bool enabled, float wet, float param,
                          bool tailAfterFader) noexcept;
  static RtMessage makeFxTempo(core::DeckId deck, double beatSeconds) noexcept;
  static RtMessage makeTrackTrim(core::DeckId deck, float db) noexcept;
  static RtMessage makeMasterProcessing(bool glue, bool limiter) noexcept;
  static RtMessage makeFxHit(core::FxHitType type, float level, double beatSeconds) noexcept;
  static RtMessage makeCrossfader(float position) noexcept;
  static RtMessage makeCrossfaderCurve(CrossfaderCurve curve) noexcept;
  static RtMessage makeChannelAssign(int channel, CrossfaderAssign assign) noexcept;
  static RtMessage makeMasterGain(float gainDb) noexcept;
  static RtMessage makeMasterLimiter(bool enabled, float ceilingDb) noexcept;
  static RtMessage makeMixerCue(int channel, bool enabled) noexcept;
  static RtMessage makeTestTone(bool enabled, float freqHz, float levelDb) noexcept;
  static RtMessage makeStemVolume(core::DeckId deck, core::StemKind stem, float volumeLinear) noexcept;
  static RtMessage makeStemMute(core::DeckId deck, core::StemKind stem, bool muted) noexcept;
  static RtMessage makeStemSolo(core::DeckId deck, core::StemKind stem, bool solo) noexcept;
  static RtMessage makeStemCue(core::DeckId deck, core::StemKind stem, bool cue) noexcept;
};

static_assert(std::is_trivially_copyable_v<RtMessage>, "RtMessage must be trivially copyable for lock-free queues");

/// Lock-free single-producer single-consumer ring buffer.
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

 public:
  SpscRingBuffer() = default;

  /// Called by the producer (control/UI thread). Realtime safe, never blocks.
  bool push(const T& item) noexcept {
    const std::size_t currentWrite = writePos_.load(std::memory_order_relaxed);
    const std::size_t currentRead = readPos_.load(std::memory_order_acquire);

    if ((currentWrite - currentRead) >= Capacity) {
      droppedCount_.fetch_add(1, std::memory_order_relaxed);
      return false;  // Queue full: drop newest
    }

    buffer_[currentWrite & (Capacity - 1)] = item;
    writePos_.store(currentWrite + 1, std::memory_order_release);
    return true;
  }

  /// Called by the consumer (audio thread). Realtime safe, never blocks.
  bool pop(T& item) noexcept {
    const std::size_t currentRead = readPos_.load(std::memory_order_relaxed);
    const std::size_t currentWrite = writePos_.load(std::memory_order_acquire);

    if (currentRead == currentWrite) {
      return false;  // Queue empty
    }

    item = buffer_[currentRead & (Capacity - 1)];
    readPos_.store(currentRead + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    const std::size_t w = writePos_.load(std::memory_order_relaxed);
    const std::size_t r = readPos_.load(std::memory_order_relaxed);
    return (w >= r) ? (w - r) : 0;
  }

  [[nodiscard]] std::uint64_t droppedCount() const noexcept { return droppedCount_.load(std::memory_order_relaxed); }

  void reset() noexcept {
    writePos_.store(0, std::memory_order_relaxed);
    readPos_.store(0, std::memory_order_relaxed);
    droppedCount_.store(0, std::memory_order_relaxed);
  }

 private:
  std::atomic<std::size_t> writePos_{0};
  char pad0_[56]{};
  std::atomic<std::size_t> readPos_{0};
  char pad1_[56]{};
  std::atomic<std::uint64_t> droppedCount_{0};
  char pad2_[56]{};
  std::array<T, Capacity> buffer_{};
};

/// Lock-free triple buffer for publishing telemetry from audio thread to UI thread without tearing.
template <typename T>
class LockFreeTripleBuffer {
 public:
  LockFreeTripleBuffer() = default;

  /// Audio thread writes without blocking or locking.
  void write(const T& value) noexcept {
    const int back = backIndex_;
    slots_[static_cast<std::size_t>(back)] = value;
    const int oldMiddle = middleIndex_.exchange(back, std::memory_order_acq_rel);
    backIndex_ = oldMiddle;
    hasNewData_.store(true, std::memory_order_release);
  }

  /// UI thread reads latest telemetry without blocking or locking.
  bool read(T& out) noexcept {
    if (hasNewData_.exchange(false, std::memory_order_acq_rel)) {
      const int oldMiddle = middleIndex_.exchange(frontIndex_, std::memory_order_acq_rel);
      frontIndex_ = oldMiddle;
      out = slots_[static_cast<std::size_t>(frontIndex_)];
      return true;
    }
    out = slots_[static_cast<std::size_t>(frontIndex_)];
    return false;
  }

 private:
  std::array<T, 3> slots_{};
  int frontIndex_{0};
  int backIndex_{1};
  std::atomic<int> middleIndex_{2};
  std::atomic<bool> hasNewData_{false};
};

/// Bridge connecting the CommandBus to the realtime audio thread and publishing telemetry (ROADMAP P2-05).
class CommandBridge {
 public:
  static constexpr std::size_t kQueueCapacity = 1024;

  CommandBridge() = default;
  ~CommandBridge() = default;

  /// Translates a high-level Command to an RtMessage. Returns nullopt if no audio-thread action is needed.
  [[nodiscard]] static std::optional<RtMessage> translateCommand(const core::Command& command) noexcept;

  /// Control/UI thread: translates and enqueues a Command. Returns false if queue is full.
  bool pushCommand(const core::Command& command) noexcept;

  /// Control/UI thread: enqueues a raw RtMessage. Returns false if queue is full.
  bool pushMessage(const RtMessage& message) noexcept;

  /// Audio thread: pops the next message. Returns false when queue is empty.
  bool popMessage(RtMessage& message) noexcept;

  /// Total number of dropped messages due to queue overflow.
  [[nodiscard]] std::uint64_t droppedMessagesCount() const noexcept;

  /// Current number of pending messages in the queue.
  [[nodiscard]] std::size_t queueSize() const noexcept;

  /// Audio thread: publishes a telemetry snapshot.
  void publishTelemetry(const AudioTelemetry& telemetry) noexcept;

  /// UI thread: reads the most recent telemetry snapshot.
  bool readTelemetry(AudioTelemetry& telemetry) noexcept;

  /// Resets queue and counters.
  void reset() noexcept;

 private:
  SpscRingBuffer<RtMessage, kQueueCapacity> queue_;
  LockFreeTripleBuffer<AudioTelemetry> telemetryBuffer_;
};

}  // namespace zyron::audio
