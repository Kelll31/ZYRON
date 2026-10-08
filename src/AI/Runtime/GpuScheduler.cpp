// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Runtime/GpuScheduler.hpp"

#include <algorithm>
#include <utility>

namespace zyron::ai {

GpuScheduler::GpuScheduler(std::shared_ptr<GPUBackend> gpuBackend, std::shared_ptr<GPUBackend> cpuBackend,
                           GpuSchedulerConfig config)
    : gpuBackend_(std::move(gpuBackend)), cpuBackend_(std::move(cpuBackend)), config_(config) {
  // 1. Enumerate GPU devices
  if (gpuBackend_ != nullptr && gpuBackend_->isAvailable()) {
    const auto devices = gpuBackend_->getDevices();
    for (const auto& dev : devices) {
      WorkerContext worker;
      worker.deviceIndex = dev.index;
      worker.deviceName = dev.name;
      worker.isGpu = true;
      worker.totalMemoryBytes = dev.vramTotalBytes > 0 ? dev.vramTotalBytes : (24ULL * 1024ULL * 1024ULL * 1024ULL);
      worker.allocatedBudgetBytes = 0;
      worker.isBusy = false;
      worker.currentJobId = 0;
      workers_.push_back(std::move(worker));
    }
  }

  // 2. Add CPU worker fallback if requested or if no GPUs are available
  if (config_.allowCpuFallback && (cpuBackend_ != nullptr || workers_.empty())) {
    WorkerContext cpuWorker;
    cpuWorker.deviceIndex = -1;
    cpuWorker.deviceName = (cpuBackend_ != nullptr) ? cpuBackend_->name() : "Host CPU Worker";
    cpuWorker.isGpu = false;
    cpuWorker.totalMemoryBytes =
        (cpuBackend_ != nullptr) ? cpuBackend_->totalMemoryBytes(0) : (16ULL * 1024ULL * 1024ULL * 1024ULL);
    cpuWorker.allocatedBudgetBytes = 0;
    cpuWorker.isBusy = false;
    cpuWorker.currentJobId = 0;
    workers_.push_back(std::move(cpuWorker));
  }

  // 3. Start worker threads
  for (std::size_t i = 0; i < workers_.size(); ++i) {
    workers_[i].thread = std::thread([this, idx = static_cast<int>(i)] { workerLoop(idx); });
  }
}

GpuScheduler::~GpuScheduler() {
  stop();
}

void GpuScheduler::stop() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_.exchange(true)) {
      return;  // Already stopped
    }

    // Cancel all waiting jobs
    for (auto& job : queue_) {
      job->status = JobStatus::Cancelled;
      if (job->cancelToken) {
        job->cancelToken->store(true, std::memory_order_relaxed);
      }
      history_.push_back(job);
    }
    queue_.clear();
  }

  cv_.notify_all();

  for (auto& worker : workers_) {
    if (worker.thread.joinable()) {
      worker.thread.join();
    }
  }
}

JobId GpuScheduler::submit(JobDefinition job) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const JobId id = nextJobId_++;

  auto record = std::make_shared<JobRecord>();
  record->id = id;
  record->definition = std::move(job);
  record->status = JobStatus::Queued;
  record->cancelToken = std::make_shared<std::atomic<bool>>(false);
  record->assignedDevice = -1;

  if (record->definition.priority == PriorityClass::Interactive) {
    // Insert interactive jobs ahead of background jobs
    auto it = std::find_if(queue_.begin(), queue_.end(), [](const std::shared_ptr<JobRecord>& item) {
      return item->definition.priority == PriorityClass::Background;
    });
    queue_.insert(it, record);
  } else {
    queue_.push_back(record);
  }

  history_.push_back(record);
  cv_.notify_all();
  return id;
}

bool GpuScheduler::cancel(JobId id) {
  std::shared_ptr<JobRecord> targetJob;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(queue_.begin(), queue_.end(), [id](const auto& item) { return item->id == id; });
    if (it != queue_.end()) {
      targetJob = *it;
      targetJob->status = JobStatus::Cancelled;
      if (targetJob->cancelToken) {
        targetJob->cancelToken->store(true, std::memory_order_relaxed);
      }
      queue_.erase(it);
    } else {
      auto histIt =
          std::find_if(history_.begin(), history_.end(), [id](const auto& item) { return item->id == id; });
      if (histIt != history_.end() && (*histIt)->status == JobStatus::Running) {
        targetJob = *histIt;
        if (targetJob->cancelToken) {
          targetJob->cancelToken->store(true, std::memory_order_relaxed);
        }
      }
    }
  }

  if (targetJob && targetJob->definition.onComplete) {
    targetJob->definition.onComplete(false, "Job cancelled by user");
    return true;
  }
  return targetJob != nullptr;
}

JobStatus GpuScheduler::jobStatus(JobId id) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(history_.begin(), history_.end(), [id](const auto& item) { return item->id == id; });
  if (it != history_.end()) {
    return (*it)->status;
  }
  return JobStatus::Failed;
}

std::vector<DeviceStatus> GpuScheduler::deviceStatuses() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  std::vector<DeviceStatus> results;
  results.reserve(workers_.size());

  for (const auto& w : workers_) {
    DeviceStatus s;
    s.deviceIndex = w.deviceIndex;
    s.name = w.deviceName;
    s.isGpu = w.isGpu;
    s.totalVramBytes = w.totalMemoryBytes;
    s.allocatedVramBudgetBytes = w.allocatedBudgetBytes;
    s.isBusy = w.isBusy;
    s.activeJobId = w.currentJobId;

    if (w.isGpu && gpuBackend_ != nullptr) {
      s.freeVramBytes = gpuBackend_->freeMemoryBytes(w.deviceIndex);
    } else if (!w.isGpu && cpuBackend_ != nullptr) {
      s.freeVramBytes = cpuBackend_->freeMemoryBytes(0);
    } else {
      s.freeVramBytes = (w.totalMemoryBytes > w.allocatedBudgetBytes) ? (w.totalMemoryBytes - w.allocatedBudgetBytes) : 0;
    }
    results.push_back(s);
  }
  return results;
}

std::size_t GpuScheduler::queuedJobCount() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

std::size_t GpuScheduler::activeJobCount() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::size_t>(
      std::count_if(workers_.begin(), workers_.end(), [](const WorkerContext& w) { return w.isBusy; }));
}

bool GpuScheduler::canRunOnDevice(const JobRecord& job, const WorkerContext& worker) const {
  if (worker.isBusy) {
    return false;
  }

  if (worker.isGpu) {
    // VRAM headroom and budget check
    if (worker.allocatedBudgetBytes + job.definition.vramBudgetBytes + config_.vramHeadroomBytes >
        worker.totalMemoryBytes) {
      return false;
    }

    const auto priority = job.definition.priority;
    if (priority == PriorityClass::Interactive) {
      // Interactive jobs prefer interactiveGpuIndex
      if (worker.deviceIndex == config_.interactiveGpuIndex) {
        return true;
      }
      // Spillover to alternate GPU if interactive GPU is busy or absent
      if (config_.allowSpillover) {
        auto interIt = std::find_if(workers_.begin(), workers_.end(),
                                    [this](const WorkerContext& w) { return w.deviceIndex == config_.interactiveGpuIndex; });
        if (interIt == workers_.end() || interIt->isBusy) {
          return true;
        }
      }
      return false;
    }

    // Background jobs prefer backgroundGpuIndex
    if (priority == PriorityClass::Background) {
      if (worker.deviceIndex == config_.backgroundGpuIndex) {
        return true;
      }
      // Can spillover to interactive GPU ONLY if background GPU is busy or absent, AND no Interactive jobs pending
      if (config_.allowSpillover && worker.deviceIndex == config_.interactiveGpuIndex) {
        auto bgIt = std::find_if(workers_.begin(), workers_.end(),
                                 [this](const WorkerContext& w) { return w.deviceIndex == config_.backgroundGpuIndex; });
        const bool bgUnavailableOrBusy = (bgIt == workers_.end() || bgIt->isBusy);
        if (!bgUnavailableOrBusy) {
          return false;  // Background GPU is idle; do not steal its job
        }
        const bool hasInteractivePending =
            std::any_of(queue_.begin(), queue_.end(), [](const std::shared_ptr<JobRecord>& r) {
              return r->definition.priority == PriorityClass::Interactive;
            });
        return !hasInteractivePending;
      }
      return false;
    }
  } else {
    // CPU fallback worker
    if (!config_.allowCpuFallback) {
      return false;
    }
    // CPU runs jobs if no GPUs exist, or if GPU workers cannot run the job
    const bool hasGpu = std::any_of(workers_.begin(), workers_.end(), [](const WorkerContext& w) { return w.isGpu; });
    if (!hasGpu) {
      return true;
    }
    // On multi-GPU systems, CPU only picks up when explicitly fallback
    return false;
  }

  return false;
}

void GpuScheduler::workerLoop(int workerIndex) {
  while (!stopping_.load(std::memory_order_relaxed)) {
    std::shared_ptr<JobRecord> jobToRun;

    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this, workerIndex] {
        if (stopping_.load(std::memory_order_relaxed)) {
          return true;
        }
        auto& worker = workers_[static_cast<std::size_t>(workerIndex)];
        return std::any_of(queue_.begin(), queue_.end(),
                           [this, &worker](const auto& j) { return canRunOnDevice(*j, worker); });
      });

      if (stopping_.load(std::memory_order_relaxed)) {
        break;
      }

      auto& worker = workers_[static_cast<std::size_t>(workerIndex)];

      // 1. First priority: Interactive jobs that can run on this worker
      auto it = std::find_if(queue_.begin(), queue_.end(), [this, &worker](const auto& j) {
        return j->definition.priority == PriorityClass::Interactive && canRunOnDevice(*j, worker);
      });

      // 2. Second priority: Background jobs
      if (it == queue_.end()) {
        it = std::find_if(queue_.begin(), queue_.end(),
                          [this, &worker](const auto& j) { return canRunOnDevice(*j, worker); });
      }

      if (it != queue_.end()) {
        jobToRun = *it;
        queue_.erase(it);

        jobToRun->status = JobStatus::Running;
        jobToRun->assignedDevice = worker.deviceIndex;

        worker.isBusy = true;
        worker.currentJobId = jobToRun->id;
        worker.allocatedBudgetBytes += jobToRun->definition.vramBudgetBytes;
      }
    }

    if (!jobToRun) {
      continue;
    }

    // Execute task without holding mutex
    bool success = false;
    std::string errorMessage;

    try {
      auto reportProg = [jobToRun](float p) {
        if (jobToRun->definition.onProgress) {
          jobToRun->definition.onProgress(p);
        }
      };

      if (jobToRun->definition.execute) {
        success = jobToRun->definition.execute(jobToRun->assignedDevice, reportProg, *jobToRun->cancelToken);
      }
    } catch (const std::exception& ex) {
      success = false;
      errorMessage = ex.what();
    } catch (...) {
      success = false;
      errorMessage = "Unknown exception during GPU job execution";
    }

    // Complete job
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      auto& worker = workers_[static_cast<std::size_t>(workerIndex)];

      if (jobToRun->cancelToken->load(std::memory_order_relaxed)) {
        jobToRun->status = JobStatus::Cancelled;
      } else {
        jobToRun->status = success ? JobStatus::Completed : JobStatus::Failed;
      }

      worker.isBusy = false;
      worker.currentJobId = 0;
      worker.allocatedBudgetBytes -= jobToRun->definition.vramBudgetBytes;
    }

    if (jobToRun->definition.onComplete) {
      jobToRun->definition.onComplete(success, errorMessage);
    }

    cv_.notify_all();
  }
}

}  // namespace zyron::ai
