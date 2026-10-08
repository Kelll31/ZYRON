// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "Core/AI/AiSafetyTypes.hpp"
#include "Core/Commands/CommandBus.hpp"

namespace zyron::ai {

/// AI command safety controller, rate limiter and kill switch (SPEC section 76, ROADMAP P8-05).
class AiSafetyController final : public core::IAiSafetyController {
 public:
  explicit AiSafetyController(
      core::CommandBus& commandBus,
      core::AiSafetyConfig config = {});
  ~AiSafetyController() override = default;

  bool evaluateAndSubmit(
      const core::Command& command,
      const core::CommandOrigin& origin,
      double currentTimeSec = 0.0) override;

  void triggerKillSwitch(std::string reason) override;
  void resetKillSwitch() override;
  [[nodiscard]] bool isKillSwitchEngaged() const noexcept override;

  void setDryRunMode(bool enabled) override;
  [[nodiscard]] bool isDryRunMode() const noexcept override;

  [[nodiscard]] std::vector<core::AiAuditEntry> getAuditLog() const override;
  void clearAuditLog() override;

  [[nodiscard]] std::string killSwitchReason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return killSwitchReason_;
  }

 private:
  core::CommandBus& commandBus_;
  core::AiSafetyConfig config_;

  mutable std::mutex mutex_;
  std::atomic<bool> killSwitchEngaged_{false};
  std::string killSwitchReason_;

  std::uint64_t nextSequence_{1};
  std::vector<core::AiAuditEntry> auditLog_;
  std::vector<double> recentCommandTimesSec_;
};

}  // namespace zyron::ai
