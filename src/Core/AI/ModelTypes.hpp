// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zyron::core {

/// High-level AI task type (SPEC section 74).
enum class ModelTask : std::uint8_t {
  StemSeparation = 0,
  BeatDetection,
  KeyDetection,
  ChordRecognition,
  StructureSegmentation
};

[[nodiscard]] constexpr std::string_view modelTaskName(ModelTask task) noexcept {
  switch (task) {
    case ModelTask::StemSeparation:
      return "Stem Separation";
    case ModelTask::BeatDetection:
      return "Beat Detection";
    case ModelTask::KeyDetection:
      return "Key Detection";
    case ModelTask::ChordRecognition:
      return "Chord Recognition";
    case ModelTask::StructureSegmentation:
      return "Structure Segmentation";
  }
  return "Unknown";
}

/// Installation and validation lifecycle status for an AI model (SPEC section 74).
enum class ModelInstallStatus : std::uint8_t {
  NotInstalled = 0,
  Installed,
  Downloading,
  Corrupted,
  Ready
};

[[nodiscard]] constexpr std::string_view modelInstallStatusName(ModelInstallStatus status) noexcept {
  switch (status) {
    case ModelInstallStatus::NotInstalled:
      return "Not Installed";
    case ModelInstallStatus::Installed:
      return "Installed";
    case ModelInstallStatus::Downloading:
      return "Downloading";
    case ModelInstallStatus::Corrupted:
      return "Corrupted";
    case ModelInstallStatus::Ready:
      return "Ready";
  }
  return "Unknown";
}

/// AI model descriptor, license details, and runtime compatibility metadata (SPEC section 74, ADR-0013).
struct ModelMetadata {
  std::string id;
  std::string name;
  std::string version;
  ModelTask task{ModelTask::StemSeparation};
  std::string filename;
  std::string sourceUrl;
  std::string license;
  bool isPermissive{true};
  std::uint64_t sizeBytes{0};
  std::string expectedSha256;
  std::uint64_t minVramBytes{0};
  std::string supportedBackends{"CUDA, CPU"};
  ModelInstallStatus status{ModelInstallStatus::NotInstalled};
  std::string localFilePath;
  bool gpuCompatible{true};
};

/// Abstract interface for querying and managing AI models (SPEC sections 73, 74).
class IModelManager {
 public:
  virtual ~IModelManager() = default;

  [[nodiscard]] virtual std::vector<ModelMetadata> availableModels() const = 0;
  [[nodiscard]] virtual ModelInstallStatus status(const std::string& modelId) const = 0;
  [[nodiscard]] virtual std::string modelPath(const std::string& modelId) const = 0;
  virtual bool importModel(const std::string& modelId, const std::string& sourcePath) = 0;
  virtual bool removeModel(const std::string& modelId) = 0;
  virtual void refresh() = 0;
};

}  // namespace zyron::core
