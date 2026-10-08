// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AI/Backends/GPUBackend.hpp"
#include "Core/System/HardwareInfo.hpp"

namespace zyron::ai {

/// Priority classes for AI workloads (SPEC section 36, ARCHITECTURE section 10).
enum class PriorityClass : std::uint8_t {
  Interactive = 0,  // High priority: real-time stem extraction on active deck, live analysis
  Background = 1    // Low priority: library background analysis, batch stem caching, embeddings
};

enum class JobStatus : std::uint8_t {
  Queued = 0,
  Running,
  Completed,
  Cancelled,
  Failed
};

using JobId = std::uint64_t;

/// Workload description submitted to the GPU scheduler.
struct JobDefinition {
  std::string name;
  PriorityClass priority{PriorityClass::Background};
  std::uint64_t vramBudgetBytes{0};  // Estimated VRAM requirements (model + activations)

  /// Task execution function. Arguments:
  ///  - deviceIndex: Assigned GPU index (0..N-1) or -1 for host CPU
  ///  - reportProgress: Thread-safe callback for progress updates (0.0 to 1.0)
  ///  - cancelToken: Atomic token polled periodically to detect cancellation
  std::function<bool(int deviceIndex, std::function<void(float progress)> reportProgress,
                     const std::atomic<bool>& cancelToken)>
      execute;

  std::function<void(float progress)> onProgress;
  std::function<void(bool success, const std::string& error)> onComplete;
};

/// Configuration for multi-GPU priority scheduling policy (SPEC section 36).
struct GpuSchedulerConfig {
  int interactiveGpuIndex{0};       // GPU A preferred for interactive jobs
  int backgroundGpuIndex{1};        // GPU B preferred for background jobs
  std::uint64_t vramHeadroomBytes{512ULL * 1024ULL * 1024ULL};  // 512 MB reserve for display/system
  bool allowSpillover{true};        // Allow jobs to spill to alternate GPU when preferred is busy
  bool allowCpuFallback{true};      // Allow CPU fallback worker when GPUs are unavailable or busy
};

/// Snapshot of a compute device's workload and VRAM status.
struct DeviceStatus {
  int deviceIndex{-1};              // 0+ for GPU, -1 for CPU
  std::string name;
  bool isGpu{false};
  std::uint64_t totalVramBytes{0};
  std::uint64_t freeVramBytes{0};
  std::uint64_t allocatedVramBudgetBytes{0};
  bool isBusy{false};
  JobId activeJobId{0};
};

/// Priority-aware Multi-GPU and CPU task scheduler (SPEC sections 35, 36, 41, ARCHITECTURE section 10).
///
/// Features:
///  - Dedicated background worker per GPU + CPU fallback worker
///  - Priority queues: Interactive jobs take precedence over Background jobs
///  - Intelligent dual-GPU routing: Interactive -> GPU 0, Background -> GPU 1
///  - Dynamic spillover to idle GPU when preferred device is saturated
///  - VRAM budget tracking with safety headroom
///  - Cooperative job cancellation
///  - Completely non-blocking to UI and realtime audio threads
class GpuScheduler {
 public:
  explicit GpuScheduler(std::shared_ptr<GPUBackend> gpuBackend,
                        std::shared_ptr<GPUBackend> cpuBackend = nullptr,
                        GpuSchedulerConfig config = {});
  ~GpuScheduler();

  GpuScheduler(const GpuScheduler&) = delete;
  GpuScheduler& operator=(const GpuScheduler&) = delete;

  /// Submits a new job. Returns a unique JobId. Thread-safe, non-blocking.
  JobId submit(JobDefinition job);

  /// Requests cooperative cancellation of a queued or running job. Thread-safe.
  bool cancel(JobId id);

  /// Queries the current status of a job. Thread-safe.
  [[nodiscard]] JobStatus jobStatus(JobId id) const;

  /// Returns real-time status snapshots of all managed devices. Thread-safe.
  [[nodiscard]] std::vector<DeviceStatus> deviceStatuses() const;

  /// Returns number of jobs waiting in the queue. Thread-safe.
  [[nodiscard]] std::size_t queuedJobCount() const;

  /// Returns number of jobs currently executing on workers. Thread-safe.
  [[nodiscard]] std::size_t activeJobCount() const;

  /// Gracefully cancels pending jobs and stops all worker threads.
  void stop();

 private:
  struct JobRecord {
    JobId id{0};
    JobDefinition definition;
    JobStatus status{JobStatus::Queued};
    std::shared_ptr<std::atomic<bool>> cancelToken;
    int assignedDevice{-1};
  };

  struct WorkerContext {
    int deviceIndex{-1};  // 0+ for GPU, -1 for CPU
    std::string deviceName;
    bool isGpu{false};
    std::uint64_t totalMemoryBytes{0};
    std::uint64_t allocatedBudgetBytes{0};
    bool isBusy{false};
    JobId currentJobId{0};
    std::thread thread;
  };

  void workerLoop(int workerIndex);
  bool canRunOnDevice(const JobRecord& job, const WorkerContext& worker) const;

  std::shared_ptr<GPUBackend> gpuBackend_;
  std::shared_ptr<GPUBackend> cpuBackend_;
  GpuSchedulerConfig config_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> stopping_{false};

  JobId nextJobId_{1};
  std::vector<std::shared_ptr<JobRecord>> queue_;
  std::vector<std::shared_ptr<JobRecord>> history_;
  std::vector<WorkerContext> workers_;
};

}  // namespace zyron::ai
