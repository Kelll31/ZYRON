// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/AI/ModelTypes.hpp"

namespace zyron::core {

/// Wizard steps for initial application setup (SPEC section 73).
enum class FirstRunStep : std::uint8_t {
  WelcomeAndPolicy = 0,
  ModelLocation,
  ModelVerification,
  ReadyToLaunch
};

[[nodiscard]] constexpr std::string_view firstRunStepName(FirstRunStep step) noexcept {
  switch (step) {
    case FirstRunStep::WelcomeAndPolicy:
      return "Welcome & Offline Policy";
    case FirstRunStep::ModelLocation:
      return "Model Storage Location";
    case FirstRunStep::ModelVerification:
      return "AI Models Verification";
    case FirstRunStep::ReadyToLaunch:
      return "Ready To Launch";
  }
  return "Unknown";
}

/// Offline network policy consent option (SPEC section 75).
enum class OfflinePolicyConsent : std::uint8_t {
  OfflineOnly = 0,     // Strictly zero network connections.
  AllowNetworkChecks   // Allow network for version check & model downloading only.
};

/// High-level model status during initial onboarding.
struct FirstRunModelSummary {
  std::string modelId;
  std::string name;
  std::string filename;
  std::uint64_t sizeBytes{0};
  ModelInstallStatus status{ModelInstallStatus::NotInstalled};
  std::string verificationMessage;
};

/// Persisted configuration record for first-run onboarding.
struct FirstRunConfig {
  bool isCompleted{false};
  bool offlineMode{true};
  std::string modelsDirectory;
  std::string completedTimestamp;
};

/// Manager interface driving the first-run onboarding flow (SPEC section 73).
class IFirstRunManager {
 public:
  virtual ~IFirstRunManager() = default;

  [[nodiscard]] virtual FirstRunStep currentStep() const noexcept = 0;
  virtual void setStep(FirstRunStep step) = 0;

  [[nodiscard]] virtual bool isFirstRunNeeded() const = 0;
  [[nodiscard]] virtual const FirstRunConfig& config() const noexcept = 0;

  virtual void setOfflinePolicy(OfflinePolicyConsent consent) = 0;
  virtual bool setModelsDirectory(const std::string& path) = 0;

  [[nodiscard]] virtual std::vector<FirstRunModelSummary> scanAndVerifyModels() = 0;
  virtual bool importModel(const std::string& modelId, const std::string& sourcePath) = 0;
  virtual bool downloadModel(const std::string& modelId,
                             std::function<void(float progress)> onProgress = nullptr) = 0;

  virtual bool completeFirstRun() = 0;
  virtual void resetFirstRun() = 0;
};

}  // namespace zyron::core
