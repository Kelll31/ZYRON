// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#include "Application/NeuralModels.hpp"
#include "Audio/Engine/AudioEngine.hpp"
#include "Library/LibraryService.hpp"
#include "Stems/Cache/StemCache.hpp"

namespace zyron::application {

/// Separates tracks into vocals, drums, bass and other with HTDemucs in the background (SPEC sections 32, 42, 43) and
/// hands the stems to the engine, which switches the deck over without a gap.
///
/// One job at a time, on its own thread; the result is cached on disk by content hash, model and version, so a track
/// is separated once and its stems appear at once the next time it is loaded. Progress and failures are reported
/// through the engine's deck status, which the UI shows on the SPLIT button.
class StemService {
 public:
  StemService(audio::AudioEngine& engine, library::LibraryService* library, NeuralModels& models,
              const std::filesystem::path& cacheDirectory);
  ~StemService();

  StemService(const StemService&) = delete;
  StemService& operator=(const StemService&) = delete;

  /// Queues a separation of the track on `deck` (a no-op status update if the model is unavailable).
  void request(core::DeckId deck, core::TrackId track);
  /// Called when a track finished loading: attaches cached stems if there are any (otherwise nothing happens).
  void trackReady(core::DeckId deck, core::TrackId track);

 private:
  struct Job {
    core::DeckId deck;
    core::TrackId track;
    bool cachedOnly;  // true: attach cached stems and stay silent when there are none
  };

  void enqueue(const Job& job);
  void workerLoop();
  void process(const Job& job);
  [[nodiscard]] std::string cacheKey(core::TrackId track) const;
  /// Converts separated stems to the deck's sample rate and length and gives them to the engine.
  bool attach(const Job& job, stems::StemSeparationResult& result);

  audio::AudioEngine& engine_;
  library::LibraryService* library_;
  NeuralModels& models_;
  stems::StemCache cache_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Job> queue_;
  bool stopping_{false};
  std::thread worker_;
};

}  // namespace zyron::application
