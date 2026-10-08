// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zyron::core {

/// Task priority level for background and AI processing (SPEC sections 36, 41).
enum class QueuePriority : std::uint8_t {
  Interactive = 0,
  Background = 1
};

[[nodiscard]] constexpr std::string_view queuePriorityName(QueuePriority p) noexcept {
  switch (p) {
    case QueuePriority::Interactive:
      return "Interactive";
    case QueuePriority::Background:
      return "Background";
  }
  return "Unknown";
}

/// Execution lifecycle state for a background task (SPEC section 41).
enum class QueueItemStatus : std::uint8_t {
  Queued = 0,
  Running,
  Completed,
  Cancelled,
  Failed
};

[[nodiscard]] constexpr std::string_view queueItemStatusName(QueueItemStatus s) noexcept {
  switch (s) {
    case QueueItemStatus::Queued:
      return "Queued";
    case QueueItemStatus::Running:
      return "Running";
    case QueueItemStatus::Completed:
      return "Completed";
    case QueueItemStatus::Cancelled:
      return "Cancelled";
    case QueueItemStatus::Failed:
      return "Failed";
  }
  return "Unknown";
}

/// Snapshot description of an active or recent background job.
struct QueueJobSnapshot {
  std::uint64_t id{0};
  std::string name;
  QueuePriority priority{QueuePriority::Background};
  QueueItemStatus status{QueueItemStatus::Queued};
  float progress{0.0F};  // In [0.0, 1.0]
  int deviceIndex{-1};   // 0+ for GPU, -1 for CPU
  std::string deviceName{"CPU"};
  std::string errorMessage;
};

/// Abstract interface for inspecting and controlling the AI background queue (SPEC sections 41, 65).
class IBackgroundQueueManager {
 public:
  virtual ~IBackgroundQueueManager() = default;

  [[nodiscard]] virtual std::vector<QueueJobSnapshot> allJobs() const = 0;
  [[nodiscard]] virtual std::size_t activeJobCount() const = 0;
  virtual bool cancelJob(std::uint64_t id) = 0;
  virtual void clearFinishedJobs() = 0;
};

}  // namespace zyron::core
