// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Core/Audio/AudioTap.hpp"
#include "Recording/WavFileWriter.hpp"

namespace zyron::recording {

/// Recording state enum.
enum class RecordingState : std::uint8_t { Idle, Recording, Paused, Stopping };

/// Lock-free Master Tap and background file recording engine (SPEC section 46, SPEC gap #6).
///
/// Guarantees:
///  - Audio thread writes to lock-free FIFO via IAudioTap; zero allocations, zero mutexes, zero disk I/O.
///  - Background writer thread drains FIFO and streams directly to WAV file.
///  - Buffer overflow protection with dropped frame telemetry counters.
class MasterRecorder final : public core::IAudioTap {
 public:
  static constexpr std::size_t kFifoCapacity = 65536;  // ~1.36s stereo @ 48 kHz

  MasterRecorder();
  ~MasterRecorder() override;

  MasterRecorder(const MasterRecorder&) = delete;
  MasterRecorder& operator=(const MasterRecorder&) = delete;

  /// Starts recording to destination file. Non-realtime thread.
  bool start(const std::filesystem::path& path, double sampleRate, int numChannels = 2, bool useFloat32 = true,
             std::string* errorOut = nullptr);

  /// Pauses recording (audio tap ignores incoming samples). Non-realtime thread.
  void pause() noexcept;

  /// Resumes recording. Non-realtime thread.
  void resume() noexcept;

  /// Stops recording, flushes FIFO, finalizes file headers. Non-realtime thread.
  void stop() noexcept;

  // core::IAudioTap: Realtime audio thread callback. Realtime safe.
  void writeSamples(const float* const* channels, int numChannels, int numSamples) noexcept override;

  // Status & Telemetry
  [[nodiscard]] bool isRecording() const noexcept;
  [[nodiscard]] bool isPaused() const noexcept;
  [[nodiscard]] std::int64_t recordedFrames() const noexcept;
  [[nodiscard]] double recordedDurationSec() const noexcept;
  [[nodiscard]] std::uint64_t overflowDropsCount() const noexcept;
  [[nodiscard]] std::filesystem::path currentFilePath() const;

 private:
  void writerLoop();
  void drainFifo();

  std::atomic<RecordingState> state_{RecordingState::Idle};
  std::atomic<double> sampleRate_{48000.0};
  std::atomic<int> numChannels_{2};
  std::atomic<std::uint64_t> overflowDrops_{0};

  // Lock-free SPSC FIFO for interleaved float samples
  std::vector<float> fifoBuffer_;
  std::atomic<std::size_t> writeIndex_{0};
  char pad0_[56]{};
  std::atomic<std::size_t> readIndex_{0};
  char pad1_[56]{};

  // Background writer thread
  std::thread writerThread_;
  std::mutex writerMutex_;
  std::condition_variable writerCv_;
  std::atomic<bool> threadRunning_{false};

  WavFileWriter wavWriter_;
};

}  // namespace zyron::recording
