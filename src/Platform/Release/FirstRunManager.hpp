// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Core/AI/ModelTypes.hpp"
#include "Core/Release/FirstRunTypes.hpp"
#include "Core/Release/UpdateTypes.hpp"

namespace zyron::platform {

/// Production first-run onboarding manager (SPEC sections 73, 75, ROADMAP P10-02).
class FirstRunManager : public core::IFirstRunManager {
 public:
  explicit FirstRunManager(std::string configFilePath = "",
                          std::shared_ptr<core::IModelManager> modelManager = nullptr,
                          std::shared_ptr<core::IHttpTransport> transport = nullptr);
  ~FirstRunManager() override = default;

  [[nodiscard]] core::FirstRunStep currentStep() const noexcept override;
  void setStep(core::FirstRunStep step) override;

  [[nodiscard]] bool isFirstRunNeeded() const override;
  [[nodiscard]] const core::FirstRunConfig& config() const noexcept override;

  void setOfflinePolicy(core::OfflinePolicyConsent consent) override;
  bool setModelsDirectory(const std::string& path) override;

  [[nodiscard]] std::vector<core::FirstRunModelSummary> scanAndVerifyModels() override;
  bool importModel(const std::string& modelId, const std::string& sourcePath) override;
  bool downloadModel(const std::string& modelId,
                     std::function<void(float progress)> onProgress = nullptr) override;

  bool completeFirstRun() override;
  void resetFirstRun() override;

  /// Loads configuration from JSON file.
  bool loadConfig();
  /// Saves configuration to JSON file.
  bool saveConfig() const;

 private:
  mutable std::mutex mutex_;
  std::string configFilePath_;
  core::FirstRunConfig config_;
  core::FirstRunStep currentStep_{core::FirstRunStep::WelcomeAndPolicy};
  std::shared_ptr<core::IModelManager> modelManager_;
  std::shared_ptr<core::IHttpTransport> transport_;
};

}  // namespace zyron::platform
