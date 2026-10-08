// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandTypes.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Configuration for AI execution safety, rate limiting and sandboxing (SPEC section 76, ROADMAP P8-05).
struct AiSafetyConfig {
  std::size_t maxCommandsPerSecond{50};   // Prevent command bus flooding
  std::size_t burstAllowance{20};         // Allowed burst queue headroom
  bool dryRunMode{false};                 // If true, validates and logs but does not submit
  bool rateLimitEnabled{true};
};

/// Immutable audit record of an AI command evaluation (SPEC section 76).
struct AiAuditEntry {
  std::uint64_t sequenceNumber{0};
  double timestampSec{0.0};
  std::string commandName;
  std::optional<DeckId> targetDeck;
  std::string summary;
  bool submitted{false};
  bool rateLimited{false};
  bool killSwitchBlocked{false};
  bool dryRun{false};
};

/// Abstract interface for AI command safety, rate limiting, and emergency kill switch (SPEC section 76, ROADMAP P8-05).
class IAiSafetyController {
 public:
  virtual ~IAiSafetyController() = default;

  /// Evaluates safety criteria (kill switch, rate limits, dry run) and submits command to bus if cleared.
  virtual bool evaluateAndSubmit(
      const Command& command,
      const CommandOrigin& origin,
      double currentTimeSec = 0.0) = 0;

  /// Immediately engages emergency kill switch, blocking all AI command execution.
  virtual void triggerKillSwitch(std::string reason) = 0;

  /// Resets emergency kill switch after human review.
  virtual void resetKillSwitch() = 0;

  /// Checks if emergency kill switch is currently engaged.
  [[nodiscard]] virtual bool isKillSwitchEngaged() const noexcept = 0;

  /// Enables or disables dry-run simulation mode.
  virtual void setDryRunMode(bool enabled) = 0;

  /// Checks whether dry-run mode is currently enabled.
  [[nodiscard]] virtual bool isDryRunMode() const noexcept = 0;

  /// Retrieves chronological copy of AI command audit entries.
  [[nodiscard]] virtual std::vector<AiAuditEntry> getAuditLog() const = 0;

  /// Clears audit trail.
  virtual void clearAuditLog() = 0;
};

}  // namespace zyron::core
