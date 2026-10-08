// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "AI/Runtime/GpuScheduler.hpp"
#include "Core/AI/BackgroundQueueTypes.hpp"

namespace zyron::ai {

/// Production bridge implementing core::IBackgroundQueueManager on top of GpuScheduler (SPEC sections 36, 41).
class BackgroundQueueManager final : public core::IBackgroundQueueManager {
 public:
  explicit BackgroundQueueManager(std::shared_ptr<GpuScheduler> scheduler);
  ~BackgroundQueueManager() override = default;

  /// Enqueues a job into the GPU scheduler and tracks its telemetry.
  core::QueueJobSnapshot enqueue(const std::string& name,
                                core::QueuePriority priority,
                                std::uint64_t vramBudgetBytes,
                                std::function<bool(int deviceIndex,
                                                   std::function<void(float progress)> reportProgress,
                                                   const std::atomic<bool>& cancelToken)> execute);

  [[nodiscard]] std::vector<core::QueueJobSnapshot> allJobs() const override;
  [[nodiscard]] std::size_t activeJobCount() const override;
  bool cancelJob(std::uint64_t id) override;
  void clearFinishedJobs() override;

  [[nodiscard]] std::shared_ptr<GpuScheduler> scheduler() const noexcept { return scheduler_; }

 private:
  std::shared_ptr<GpuScheduler> scheduler_;
  mutable std::mutex mutex_;
  std::vector<core::QueueJobSnapshot> jobs_;
};

}  // namespace zyron::ai
