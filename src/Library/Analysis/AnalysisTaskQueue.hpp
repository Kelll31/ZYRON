// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Library/Database/Database.hpp"
#include "Library/Database/DatabaseWriter.hpp"
#include "Library/Database/LibraryRepository.hpp"
#include "Library/Metadata/MetadataExtractor.hpp"

namespace zyron::library {

/// Execution context passed to an individual analysis task handler (SPEC section 31).
struct TaskContext {
  std::int64_t taskId{0};
  std::int64_t trackId{0};
  std::string taskType;
  int version{1};
  std::string filepath;
  std::string contentHash;
  std::filesystem::path cacheDirectory;
  Database* db{nullptr};
  /// Set when the queue is being stopped: a long handler should return early (it then fails with the message
  /// "cancelled" and is queued again at the next start).
  const std::atomic<bool>* cancelRequested{nullptr};
};

using TaskHandler = std::function<bool(const TaskContext& ctx, std::string& errorOut)>;

/// Asynchronous, versioned, idempotent analysis task queue manager (SPEC section 31, ARCHITECTURE section 9).
///
/// Features:
///  - Pipeline (§31): Import → Metadata → Duration → BPM → Beatgrid → Key → Energy → Waveform → Ready.
///  - Task versioning: algorithm improvements automatically re-queue only outdated tasks.
///  - Idempotency & isolation: re-runs overwrite cleanly; individual task failures leave track usable.
///  - Protected user edits: beatgrids/cues with source='user' are strictly preserved.
///  - Pluggable handler architecture with built-in duration and waveform peak handlers.
class AnalysisTaskQueue {
 public:
  AnalysisTaskQueue();
  explicit AnalysisTaskQueue(std::unique_ptr<MetadataExtractor> extractor);
  ~AnalysisTaskQueue();

  AnalysisTaskQueue(const AnalysisTaskQueue&) = delete;
  AnalysisTaskQueue& operator=(const AnalysisTaskQueue&) = delete;

  /// Registers a task handler callback and its current algorithm version.
  void registerHandler(std::string_view taskType, TaskHandler handler, int currentVersion = 1);

  /// Enqueues a specific analysis task for a track.
  void enqueueTask(Database& db, std::int64_t trackId, std::string_view taskType, int version = 1);

  /// Enqueues all standard tasks for a newly added track (§31 pipeline).
  void enqueueStandardPipeline(Database& db, std::int64_t trackId);

  /// Re-queues tasks of taskType whose stored version is older than the registered algorithm version.
  std::size_t requeueOutdatedTasks(Database& db, std::string_view taskType);

  /// Processes the next pending task synchronously. Returns true if a task was processed.
  bool processNextPendingTask(Database& db, const std::filesystem::path& cacheDir);

  /// Processes all currently pending tasks synchronously up to maxTasks (0 = unlimited).
  std::size_t processAllPendingTasks(Database& db,
                                     const std::filesystem::path& cacheDir,
                                     std::size_t maxTasks = 0);

  /// Starts the background analysis worker thread.
  void startWorker(DatabaseWriter& writer,
                   const std::filesystem::path& dbPathForReader,
                   const std::filesystem::path& cacheDir,
                   int pollIntervalMs = 50);

  /// Stops the background analysis worker thread.
  void stopWorker();

  /// Returns whether the background worker is currently active.
  [[nodiscard]] bool isWorkerRunning() const noexcept;

  /// Returns the number of currently pending tasks in the database.
  [[nodiscard]] std::size_t getPendingCount(Database& db) const;

 private:
  void setupDefaultHandlers();
  void workerLoop(DatabaseWriter* writer,
                  std::filesystem::path dbPathForReader,
                  std::filesystem::path cacheDir,
                  int pollIntervalMs);

  struct HandlerEntry {
    TaskHandler handler;
    int version{1};
  };

  std::unique_ptr<MetadataExtractor> extractor_;
  std::map<std::string, HandlerEntry, std::less<>> handlers_;
  std::atomic<bool> workerRunning_{false};
  std::atomic<bool> stopRequested_{false};
  std::thread workerThread_;
};

}  // namespace zyron::library
