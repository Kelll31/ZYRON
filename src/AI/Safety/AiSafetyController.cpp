// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Safety/AiSafetyController.hpp"

#include <algorithm>

namespace zyron::ai {

AiSafetyController::AiSafetyController(
    core::CommandBus& commandBus,
    core::AiSafetyConfig config)
    : commandBus_(commandBus), config_(config) {}

bool AiSafetyController::evaluateAndSubmit(
    const core::Command& command,
    const core::CommandOrigin& origin,
    double currentTimeSec) {
  std::lock_guard<std::mutex> lock(mutex_);

  core::AiAuditEntry entry;
  entry.sequenceNumber = nextSequence_++;
  entry.timestampSec = currentTimeSec;
  entry.commandName = std::string(core::commandName(command));
  entry.targetDeck = core::targetDeck(command);
  entry.submitted = false;
  entry.rateLimited = false;
  entry.killSwitchBlocked = false;
  entry.dryRun = false;

  // 1. Check Kill Switch
  if (killSwitchEngaged_.load(std::memory_order_acquire)) {
    entry.killSwitchBlocked = true;
    entry.summary = "Blocked by emergency kill switch: " + killSwitchReason_;
    auditLog_.push_back(std::move(entry));
    return false;
  }

  // 2. Check Rate Limits (max commands per 1.0s window)
  if (config_.rateLimitEnabled) {
    const double windowStart = currentTimeSec - 1.0;
    recentCommandTimesSec_.erase(
        std::remove_if(
            recentCommandTimesSec_.begin(),
            recentCommandTimesSec_.end(),
            [windowStart](double t) { return t < windowStart; }),
        recentCommandTimesSec_.end());

    if (recentCommandTimesSec_.size() >= config_.maxCommandsPerSecond) {
      entry.rateLimited = true;
      entry.summary = "Blocked by rate limiter (> " + std::to_string(config_.maxCommandsPerSecond) + " cmd/s)";
      auditLog_.push_back(std::move(entry));
      return false;
    }
  }

  // 3. Check Dry Run Mode
  if (config_.dryRunMode) {
    entry.dryRun = true;
    entry.summary = "Dry-run validated; command not dispatched to audio engine.";
    auditLog_.push_back(std::move(entry));
    return true;
  }

  // Record timestamp in rate limiting bucket
  recentCommandTimesSec_.push_back(currentTimeSec);

  // 4. Submit to bus
  const auto err = commandBus_.submit(command, origin);
  if (!err.has_value()) {
    entry.submitted = true;
    entry.summary = "Executed successfully.";
  } else {
    entry.submitted = false;
    entry.summary = "Rejected by CommandBus: " + err->message;
  }

  auditLog_.push_back(std::move(entry));
  return !err.has_value();
}

void AiSafetyController::triggerKillSwitch(std::string reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  killSwitchEngaged_.store(true, std::memory_order_release);
  killSwitchReason_ = std::move(reason);

  core::AiAuditEntry entry;
  entry.sequenceNumber = nextSequence_++;
  entry.commandName = "EMERGENCY_KILL_SWITCH";
  entry.killSwitchBlocked = true;
  entry.summary = "Emergency kill switch engaged: " + killSwitchReason_;
  auditLog_.push_back(std::move(entry));
}

void AiSafetyController::resetKillSwitch() {
  std::lock_guard<std::mutex> lock(mutex_);
  killSwitchEngaged_.store(false, std::memory_order_release);
  killSwitchReason_.clear();

  core::AiAuditEntry entry;
  entry.sequenceNumber = nextSequence_++;
  entry.commandName = "KILL_SWITCH_RESET";
  entry.summary = "Emergency kill switch reset by user.";
  auditLog_.push_back(std::move(entry));
}

bool AiSafetyController::isKillSwitchEngaged() const noexcept {
  return killSwitchEngaged_.load(std::memory_order_acquire);
}

void AiSafetyController::setDryRunMode(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.dryRunMode = enabled;
}

bool AiSafetyController::isDryRunMode() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_.dryRunMode;
}

std::vector<core::AiAuditEntry> AiSafetyController::getAuditLog() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return auditLog_;
}

void AiSafetyController::clearAuditLog() {
  std::lock_guard<std::mutex> lock(mutex_);
  auditLog_.clear();
  recentCommandTimesSec_.clear();
}

}  // namespace zyron::ai
