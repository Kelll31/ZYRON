// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "AI/Backends/Cpu/CpuBackend.hpp"
#include "AI/Backends/Cuda/CudaBackend.hpp"
#include "AI/Runtime/GpuScheduler.hpp"
#include "Core/System/HardwareProbe.hpp"

using namespace zyron;
using namespace zyron::ai;
using namespace zyron::core;

namespace {

class FakeGpuProbe final : public GpuProbe {
 public:
  explicit FakeGpuProbe(GpuInfo info) : info_(std::move(info)) {}

  GpuInfo probe() override { return info_; }

 private:
  GpuInfo info_;
};

GpuInfo makeDual3090Info() {
  GpuInfo info;
  info.status = GpuInfo::Status::Available;
  info.vendor = "NVIDIA";
  info.driverVersion = "576.52";
  info.computeApi = "CUDA 13.4";

  GpuDevice dev0;
  dev0.index = 0;
  dev0.name = "NVIDIA GeForce RTX 3090";
  dev0.vramTotalBytes = 24ULL * 1024ULL * 1024ULL * 1024ULL;
  dev0.vramFreeBytes = 22ULL * 1024ULL * 1024ULL * 1024ULL;
  dev0.computeCapability = "8.6";

  GpuDevice dev1;
  dev1.index = 1;
  dev1.name = "NVIDIA GeForce RTX 3090";
  dev1.vramTotalBytes = 24ULL * 1024ULL * 1024ULL * 1024ULL;
  dev1.vramFreeBytes = 23ULL * 1024ULL * 1024ULL * 1024ULL;
  dev1.computeCapability = "8.6";

  info.devices = {dev0, dev1};
  return info;
}

}  // namespace

TEST_CASE("CudaBackend: discovery, capabilities, and device inspection", "[ai][cuda]") {
  SECTION("Available with dual RTX 3090 devices") {
    auto probe = std::make_unique<FakeGpuProbe>(makeDual3090Info());
    CudaBackend backend(std::move(probe));

    REQUIRE(backend.isAvailable());
    CHECK(backend.type() == BackendType::Cuda);
    CHECK(backend.name() == "NVIDIA CUDA Backend");
    CHECK(backend.version() == "CUDA 13.4");
    CHECK(backend.supportsFp16());

    const auto devices = backend.getDevices();
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].index == 0);
    CHECK(devices[0].name == "NVIDIA GeForce RTX 3090");
    CHECK(devices[0].computeCapability == "8.6");
    CHECK(backend.totalMemoryBytes(0) == 24ULL * 1024ULL * 1024ULL * 1024ULL);
    CHECK(backend.freeMemoryBytes(0) == 22ULL * 1024ULL * 1024ULL * 1024ULL);
    CHECK(backend.totalMemoryBytes(1) == 24ULL * 1024ULL * 1024ULL * 1024ULL);
    CHECK(backend.freeMemoryBytes(1) == 23ULL * 1024ULL * 1024ULL * 1024ULL);
  }

  SECTION("Driver not present fallback") {
    GpuInfo noDriverInfo;
    noDriverInfo.status = GpuInfo::Status::NoDriver;
    noDriverInfo.detail = "No NVIDIA driver installed";

    auto probe = std::make_unique<FakeGpuProbe>(noDriverInfo);
    CudaBackend backend(std::move(probe));

    CHECK_FALSE(backend.isAvailable());
    CHECK_FALSE(backend.supportsFp16());
    CHECK(backend.getDevices().empty());
  }
}

TEST_CASE("GpuScheduler: multi-GPU priority routing and spillover", "[ai][scheduler]") {
  auto probe = std::make_unique<FakeGpuProbe>(makeDual3090Info());
  auto gpuBackend = std::make_shared<CudaBackend>(std::move(probe));
  auto cpuBackend = std::make_shared<CpuBackend>();

  GpuSchedulerConfig config;
  config.interactiveGpuIndex = 0;
  config.backgroundGpuIndex = 1;
  config.allowSpillover = true;
  config.allowCpuFallback = true;

  GpuScheduler scheduler(gpuBackend, cpuBackend, config);

  const auto statuses = scheduler.deviceStatuses();
  REQUIRE(statuses.size() >= 2);
  CHECK(statuses[0].deviceIndex == 0);
  CHECK(statuses[0].isGpu);
  CHECK(statuses[1].deviceIndex == 1);
  CHECK(statuses[1].isGpu);

  SECTION("Interactive jobs prefer GPU 0") {
    std::atomic<bool> done{false};
    std::atomic<int> executedOnDevice{-99};

    JobDefinition job;
    job.name = "Realtime Deck A Separation";
    job.priority = PriorityClass::Interactive;
    job.vramBudgetBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    job.execute = [&](int devIndex, auto /*progress*/, const auto& /*cancel*/) {
      executedOnDevice.store(devIndex);
      done.store(true);
      return true;
    };

    const auto id = scheduler.submit(std::move(job));
    CHECK(id > 0);

    for (int i = 0; i < 50 && !done.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(done.load());
    CHECK(executedOnDevice.load() == 0);  // Ran on interactive GPU 0
    CHECK(scheduler.jobStatus(id) == JobStatus::Completed);
  }

  SECTION("Background jobs prefer GPU 1") {
    std::atomic<bool> done{false};
    std::atomic<int> executedOnDevice{-99};

    JobDefinition job;
    job.name = "Library Background Stem Extraction";
    job.priority = PriorityClass::Background;
    job.vramBudgetBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    job.execute = [&](int devIndex, auto /*progress*/, const auto& /*cancel*/) {
      executedOnDevice.store(devIndex);
      done.store(true);
      return true;
    };

    const auto id = scheduler.submit(std::move(job));
    CHECK(id > 0);

    for (int i = 0; i < 50 && !done.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(done.load());
    CHECK(executedOnDevice.load() == 1);  // Ran on background GPU 1
    CHECK(scheduler.jobStatus(id) == JobStatus::Completed);
  }

  SECTION("Interactive job spills to GPU 1 when GPU 0 is busy") {
    std::atomic<bool> job0Running{false};
    std::atomic<bool> job0CanFinish{false};
    std::atomic<bool> job1Done{false};
    std::atomic<int> job1Device{-99};

    // 1. Long interactive job that occupies GPU 0
    JobDefinition job0;
    job0.name = "Long Job on GPU 0";
    job0.priority = PriorityClass::Interactive;
    job0.vramBudgetBytes = 1ULL * 1024ULL * 1024ULL * 1024ULL;
    job0.execute = [&](int devIndex, auto /*prog*/, const auto& /*cancel*/) {
      CHECK(devIndex == 0);
      job0Running.store(true);
      while (!job0CanFinish.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      return true;
    };

    scheduler.submit(std::move(job0));

    // Wait until job0 is executing on GPU 0
    for (int i = 0; i < 50 && !job0Running.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(job0Running.load());

    // 2. Second interactive job arrives while GPU 0 is busy
    JobDefinition job1;
    job1.name = "Urgent Spillover Job";
    job1.priority = PriorityClass::Interactive;
    job1.vramBudgetBytes = 1ULL * 1024ULL * 1024ULL * 1024ULL;
    job1.execute = [&](int devIndex, auto /*prog*/, const auto& /*cancel*/) {
      job1Device.store(devIndex);
      job1Done.store(true);
      return true;
    };

    scheduler.submit(std::move(job1));

    // Wait for job1 to complete via spillover
    for (int i = 0; i < 50 && !job1Done.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(job1Done.load());
    CHECK(job1Device.load() == 1);  // Spilled over to GPU 1!

    job0CanFinish.store(true);
  }

  SECTION("Job cancellation") {
    std::atomic<bool> jobStarted{false};
    std::atomic<bool> cancelObserved{false};

    JobDefinition job;
    job.name = "Cancellable Job";
    job.priority = PriorityClass::Background;
    job.execute = [&](int /*dev*/, auto /*prog*/, const auto& cancelToken) {
      jobStarted.store(true);
      for (int i = 0; i < 100; ++i) {
        if (cancelToken.load()) {
          cancelObserved.store(true);
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      return true;
    };

    const auto id = scheduler.submit(std::move(job));
    for (int i = 0; i < 50 && !jobStarted.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(jobStarted.load());

    REQUIRE(scheduler.cancel(id));

    for (int i = 0; i < 50 && !cancelObserved.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CHECK(cancelObserved.load());
    CHECK(scheduler.jobStatus(id) == JobStatus::Cancelled);
  }

  SECTION("Progress reporting") {
    std::vector<float> reportedProgress;
    std::atomic<bool> done{false};

    JobDefinition job;
    job.name = "Progress Reporting Job";
    job.priority = PriorityClass::Interactive;
    job.execute = [&](int /*dev*/, auto reportProgress, const auto& /*cancel*/) {
      reportProgress(0.25F);
      reportProgress(0.50F);
      reportProgress(0.75F);
      reportProgress(1.00F);
      done.store(true);
      return true;
    };
    job.onProgress = [&](float p) { reportedProgress.push_back(p); };

    scheduler.submit(std::move(job));
    for (int i = 0; i < 50 && !done.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(done.load());
    REQUIRE(reportedProgress.size() == 4);
    CHECK(reportedProgress[0] == 0.25F);
    CHECK(reportedProgress[3] == 1.00F);
  }
}
