// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Result of an asynchronous track loading operation.
struct TrackLoadResult {
  bool success{false};
  core::DeckId deck{core::DeckId::A};
  std::filesystem::path path;
  std::shared_ptr<const TrackBuffer> buffer{nullptr};
  std::string errorMessage;
};

/// Background track decoding and loader worker with atomic hand-off and deferred retirement (ROADMAP P2-01).
///
/// Guarantees:
///  - File I/O and decoding happen strictly on a dedicated background worker thread.
///  - Atomic hand-off to DeckPlayer via lock-free pointer exchange.
///  - Deferred freeing of previous TrackBuffers on the loader thread (never in realtime callback!).
class TrackLoader {
 public:
  static constexpr std::chrono::milliseconds kRetireGrace{1000};

  using CompletionCallback = std::function<void(const TrackLoadResult&)>;
  /// Decodes a compressed or non-WAV format. Called on the loader's worker thread only after the built-in WAV
  /// decoder declined the file. Returns nullptr (and fills the error) on failure.
  using DecodeFunction = std::function<std::shared_ptr<TrackBuffer>(const std::filesystem::path&, std::string*)>;

  TrackLoader();
  ~TrackLoader();

  TrackLoader(const TrackLoader&) = delete;
  TrackLoader& operator=(const TrackLoader&) = delete;

  /// Starts the background loader thread.
  void start();

  /// Stops the worker thread and drains tasks.
  void stop();

  /// Installs the decoder for formats the built-in WAV reader does not handle (MP3, FLAC, ...). Thread-safe.
  void setFallbackDecoder(DecodeFunction decoder);

  /// Synchronously decodes a file into a TrackBuffer on the calling thread.
  [[nodiscard]] std::shared_ptr<TrackBuffer> decodeFile(const std::filesystem::path& path,
                                                        std::string* errorOut = nullptr);

  /// Synchronously loads a track file onto a DeckPlayer with deferred retirement of old buffer.
  TrackLoadResult loadTrackSync(core::DeckId deck, const std::filesystem::path& path, DeckPlayer& player);

  /// Asynchronously queues a track load task for the background worker thread.
  void loadTrackAsync(core::DeckId deck, const std::filesystem::path& path, DeckPlayer& player,
                      CompletionCallback callback = nullptr);

  /// Unloads a track from DeckPlayer with deferred retirement of the buffer.
  void unloadTrack(core::DeckId deck, DeckPlayer& player);

  /// Hands a buffer that the audio thread may still be reading to the deferred-retirement list (freed after
  /// kRetireGrace, on the worker thread).
  void retire(std::shared_ptr<const TrackBuffer> buffer);

  /// Purges retired track buffers on this thread (called by worker or manually).
  void purgeRetiredBuffers() noexcept;

  /// Frees only buffers retired at least `kRetireGrace` ago: the audio thread may still be inside a block that
  /// started before the hand-off, holding the old raw pointer.
  void purgeExpiredBuffers() noexcept;

  /// Counter the audio callback increments. When set, a retired buffer is freed only after the callback has run at
  /// least kRetireCallbacks more times (a block that held the old pointer has certainly ended) on top of the time grace.
  void setClock(const std::atomic<std::uint64_t>* callbackCounter) noexcept { clock_ = callbackCounter; }
  static constexpr std::uint64_t kRetireCallbacks = 8;

  /// Number of currently retired buffers awaiting cleanup.
  [[nodiscard]] std::size_t retiredBuffersCount() const noexcept;

 private:
  struct LoadTask {
    core::DeckId deck;
    std::filesystem::path path;
    DeckPlayer* player{nullptr};
    CompletionCallback callback{nullptr};
  };

  void workerLoop();
  void retireBuffer(std::shared_ptr<const TrackBuffer> buffer);
  void retireReleased(DeckPlayer::Released released);

  std::mutex decoderMutex_;
  DecodeFunction fallbackDecoder_;
  std::thread workerThread_;
  std::mutex queueMutex_;
  std::condition_variable queueCv_;
  std::deque<LoadTask> tasks_;
  std::atomic<bool> running_{false};

  // Deferred buffer retirement list (cleaned up on worker thread)
  std::mutex retiredMutex_;
  struct RetiredBuffer {
    std::chrono::steady_clock::time_point retiredAt;
    std::uint64_t callbackAt{0};
    std::shared_ptr<const TrackBuffer> buffer;
  };
  std::vector<RetiredBuffer> retiredBuffers_;
  const std::atomic<std::uint64_t>* clock_{nullptr};
};

}  // namespace zyron::audio
