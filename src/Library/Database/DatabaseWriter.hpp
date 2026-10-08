// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "Library/Database/Database.hpp"

namespace zyron::library {

/// Dedicated single writer thread for all SQLite mutating transactions (ARCHITECTURE section 9).
/// Guarantees serialized non-blocking writes from UI/Scanner/Analysis threads.
class DatabaseWriter {
 public:
  explicit DatabaseWriter(const std::filesystem::path& dbPath);
  explicit DatabaseWriter(Database&& db);  // for in-memory DB testing
  ~DatabaseWriter();

  DatabaseWriter(const DatabaseWriter&) = delete;
  DatabaseWriter& operator=(const DatabaseWriter&) = delete;

  /// Posts a write task to the dedicated worker thread and returns a future for completion.
  std::future<void> post(std::function<void(Database&)> task);

  /// Posts a fire-and-forget write task.
  void postAndForget(std::function<void(Database&)> task);

  /// Waits for all currently queued tasks to finish execution.
  void flush();

  /// Stops the background writer thread and drains remaining tasks.
  void stop();

 private:
  void workerLoop();

  Database db_;
  std::queue<std::packaged_task<void(Database&)>> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_{false};
  std::thread workerThread_;
};

}  // namespace zyron::library
