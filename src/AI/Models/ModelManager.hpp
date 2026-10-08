// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "AI/Backends/GPUBackend.hpp"
#include "AI/Models/ModelManifest.hpp"
#include "Core/AI/ModelTypes.hpp"

namespace zyron::ai {

/// Production AI model manager handling discovery, import, verification, and hardware compatibility (SPEC sections 73, 74, ADR-0013).
class ModelManager final : public core::IModelManager {
 public:
  explicit ModelManager(std::string modelsDirectory = "models");
  ~ModelManager() override = default;

  [[nodiscard]] std::string modelsDirectory() const;
  void setModelsDirectory(const std::string& path);

  [[nodiscard]] std::vector<core::ModelMetadata> availableModels() const override;
  [[nodiscard]] core::ModelInstallStatus status(const std::string& modelId) const override;
  [[nodiscard]] std::string modelPath(const std::string& modelId) const override;

  bool importModel(const std::string& modelId, const std::string& sourcePath) override;
  bool removeModel(const std::string& modelId) override;
  void refresh() override;

  /// Validates whether a model can execute on a specific GPU device.
  [[nodiscard]] bool checkGpuCompatibility(const std::string& modelId,
                                           const GPUBackend& backend,
                                           int deviceIndex = 0) const;

  [[nodiscard]] ModelManifest& manifest() noexcept { return manifest_; }
  [[nodiscard]] const ModelManifest& manifest() const noexcept { return manifest_; }

  std::function<void(const std::string& modelId, core::ModelInstallStatus status)> onStatusChanged;

 private:
  mutable std::mutex mutex_;
  std::string modelsDir_;
  ModelManifest manifest_;
  std::unordered_map<std::string, core::ModelInstallStatus> statuses_;
};

}  // namespace zyron::ai
