// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Database/DatabaseWriter.hpp"

#include "Library/Database/Migrations.hpp"

namespace zyron::library {

DatabaseWriter::DatabaseWriter(const std::filesystem::path& dbPath) : db_(Database::open(dbPath)) {
  Migrations::apply(db_);
  workerThread_ = std::thread(&DatabaseWriter::workerLoop, this);
}

DatabaseWriter::DatabaseWriter(Database&& db) : db_(std::move(db)) {
  Migrations::apply(db_);
  workerThread_ = std::thread(&DatabaseWriter::workerLoop, this);
}

DatabaseWriter::~DatabaseWriter() {
  stop();
}

std::future<void> DatabaseWriter::post(std::function<void(Database&)> task) {
  std::packaged_task<void(Database&)> pkg(std::move(task));
  auto future = pkg.get_future();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stopping_) {
      queue_.push(std::move(pkg));
      cv_.notify_one();
    }
  }

  return future;
}

void DatabaseWriter::postAndForget(std::function<void(Database&)> task) {
  std::packaged_task<void(Database&)> pkg(std::move(task));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stopping_) {
      queue_.push(std::move(pkg));
      cv_.notify_one();
    }
  }
}

void DatabaseWriter::flush() {
  auto future = post([](Database&) {});
  future.wait();
}

void DatabaseWriter::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
      return;
    }
    stopping_ = true;
    cv_.notify_all();
  }

  if (workerThread_.joinable()) {
    workerThread_.join();
  }
}

void DatabaseWriter::workerLoop() {
  while (true) {
    std::packaged_task<void(Database&)> task;

    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });

      if (queue_.empty() && stopping_) {
        break;
      }

      if (!queue_.empty()) {
        task = std::move(queue_.front());
        queue_.pop();
      }
    }

    if (task.valid()) {
      task(db_);
    }
  }
}

}  // namespace zyron::library
