// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <memory>
#include <thread>

#include "AI/Backends/Cpu/CpuBackend.hpp"
#include "AI/Runtime/BackgroundQueueManager.hpp"
#include "AI/Runtime/GpuScheduler.hpp"
#include "Core/AI/BackgroundQueueTypes.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron;

TEST_CASE("BackgroundQueueManager: scheduling, progress, cancellation, and cleanup", "[ai][queue]") {
  auto cpuBackend = std::make_shared<ai::CpuBackend>();
  ai::GpuSchedulerConfig cfg;
  cfg.allowCpuFallback = true;
  cfg.allowSpillover = true;
  auto scheduler = std::make_shared<ai::GpuScheduler>(nullptr, cpuBackend, cfg);
  ai::BackgroundQueueManager queue(scheduler);

  SECTION("Enqueue job and observe lifecycle") {
    std::atomic<bool> executed{false};
    auto snapshot = queue.enqueue("Test Task", core::QueuePriority::Interactive, 0,
                                  [&](int /*deviceIndex*/, auto reportProgress, const auto& /*cancelToken*/) {
                                    reportProgress(0.5F);
                                    executed = true;
                                    reportProgress(1.0F);
                                    return true;
                                  });

    CHECK(snapshot.name == "Test Task");
    CHECK(snapshot.priority == core::QueuePriority::Interactive);

    // Wait briefly for execution
    for (int i = 0; i < 50 && !executed; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(executed);

    // Check allJobs() telemetry
    const auto jobs = queue.allJobs();
    REQUIRE_FALSE(jobs.empty());
    CHECK(jobs[0].name == "Test Task");

    // Clear finished jobs
    queue.clearFinishedJobs();
    CHECK(queue.allJobs().empty());
  }

  SECTION("Cancel a running or queued job") {
    std::atomic<bool> started{false};
    std::atomic<bool> wasCancelled{false};

    auto snapshot = queue.enqueue("Long Task", core::QueuePriority::Background, 0,
                                  [&](int /*deviceIndex*/, auto /*reportProgress*/, const auto& cancelToken) {
                                    started = true;
                                    for (int i = 0; i < 100; ++i) {
                                      if (cancelToken.load()) {
                                        wasCancelled = true;
                                        return false;
                                      }
                                      std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                    }
                                    return true;
                                  });

    for (int i = 0; i < 50 && !started; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    bool cancelled = queue.cancelJob(snapshot.id);
    CHECK(cancelled);

    for (int i = 0; i < 50 && !wasCancelled; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(wasCancelled);
  }
}
