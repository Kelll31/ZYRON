// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Runtime/BackgroundQueueManager.hpp"

#include <algorithm>

namespace zyron::ai {

BackgroundQueueManager::BackgroundQueueManager(std::shared_ptr<GpuScheduler> scheduler)
    : scheduler_(std::move(scheduler)) {}

core::QueueJobSnapshot BackgroundQueueManager::enqueue(
    const std::string& name,
    core::QueuePriority priority,
    std::uint64_t vramBudgetBytes,
    std::function<bool(int deviceIndex,
                       std::function<void(float progress)> reportProgress,
                       const std::atomic<bool>& cancelToken)> execute) {
  if (scheduler_ == nullptr) {
    core::QueueJobSnapshot failed;
    failed.name = name;
    failed.status = core::QueueItemStatus::Failed;
    failed.errorMessage = "No GPU scheduler available";
    return failed;
  }

  JobDefinition def;
  def.name = name;
  def.priority = (priority == core::QueuePriority::Interactive) ? PriorityClass::Interactive
                                                                 : PriorityClass::Background;
  def.vramBudgetBytes = vramBudgetBytes;
  def.execute = std::move(execute);

  // Track snapshot
  core::QueueJobSnapshot snapshot;
  snapshot.name = name;
  snapshot.priority = priority;
  snapshot.status = core::QueueItemStatus::Queued;
  snapshot.progress = 0.0F;

  def.onProgress = [this, name](float progress) {
    std::lock_guard lock(mutex_);
    for (auto& j : jobs_) {
      if (j.name == name && (j.status == core::QueueItemStatus::Queued || j.status == core::QueueItemStatus::Running)) {
        j.progress = progress;
        j.status = core::QueueItemStatus::Running;
      }
    }
  };

  def.onComplete = [this, name](bool ok, const std::string& err) {
    std::lock_guard lock(mutex_);
    for (auto& j : jobs_) {
      if (j.name == name && (j.status == core::QueueItemStatus::Queued || j.status == core::QueueItemStatus::Running)) {
        j.status = ok ? core::QueueItemStatus::Completed : core::QueueItemStatus::Failed;
        j.progress = ok ? 1.0F : j.progress;
        j.errorMessage = err;
      }
    }
  };

  const auto jobId = scheduler_->submit(def);
  snapshot.id = jobId;

  {
    std::lock_guard lock(mutex_);
    jobs_.push_back(snapshot);
  }

  return snapshot;
}

std::vector<core::QueueJobSnapshot> BackgroundQueueManager::allJobs() const {
  std::lock_guard lock(mutex_);
  if (scheduler_ == nullptr) {
    return jobs_;
  }

  const auto devices = scheduler_->deviceStatuses();
  auto result = jobs_;

  for (auto& item : result) {
    const auto st = scheduler_->jobStatus(item.id);
    switch (st) {
      case JobStatus::Queued:
        item.status = core::QueueItemStatus::Queued;
        break;
      case JobStatus::Running:
        item.status = core::QueueItemStatus::Running;
        break;
      case JobStatus::Completed:
        item.status = core::QueueItemStatus::Completed;
        item.progress = 1.0F;
        break;
      case JobStatus::Cancelled:
        item.status = core::QueueItemStatus::Cancelled;
        break;
      case JobStatus::Failed:
        item.status = core::QueueItemStatus::Failed;
        break;
    }

    for (const auto& dev : devices) {
      if (dev.activeJobId == item.id) {
        item.deviceIndex = dev.deviceIndex;
        item.deviceName = dev.name;
        break;
      }
    }
  }

  return result;
}

std::size_t BackgroundQueueManager::activeJobCount() const {
  const auto jobs = allJobs();
  std::size_t count = 0;
  for (const auto& j : jobs) {
    if (j.status == core::QueueItemStatus::Queued || j.status == core::QueueItemStatus::Running) {
      ++count;
    }
  }
  return count;
}

bool BackgroundQueueManager::cancelJob(std::uint64_t id) {
  if (scheduler_ != nullptr) {
    return scheduler_->cancel(id);
  }
  return false;
}

void BackgroundQueueManager::clearFinishedJobs() {
  std::lock_guard lock(mutex_);
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                             [](const core::QueueJobSnapshot& j) {
                               return j.status == core::QueueItemStatus::Completed ||
                                      j.status == core::QueueItemStatus::Cancelled ||
                                      j.status == core::QueueItemStatus::Failed;
                             }),
              jobs_.end());
}

}  // namespace zyron::ai
