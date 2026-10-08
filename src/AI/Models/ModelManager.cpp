// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Models/ModelManager.hpp"

#include <filesystem>
#include <system_error>

namespace zyron::ai {

ModelManager::ModelManager(std::string modelsDirectory)
    : modelsDir_(std::move(modelsDirectory)) {
  std::error_code ec;
  std::filesystem::create_directories(modelsDir_, ec);
  refresh();
}

std::string ModelManager::modelsDirectory() const {
  std::lock_guard lock(mutex_);
  return modelsDir_;
}

void ModelManager::setModelsDirectory(const std::string& path) {
  {
    std::lock_guard lock(mutex_);
    modelsDir_ = path;
    std::error_code ec;
    std::filesystem::create_directories(modelsDir_, ec);
  }
  refresh();
}

std::vector<core::ModelMetadata> ModelManager::availableModels() const {
  std::lock_guard lock(mutex_);
  auto list = manifest_.allModels();
  for (auto& item : list) {
    auto it = statuses_.find(item.id);
    if (it != statuses_.end()) {
      item.status = it->second;
    } else {
      item.status = core::ModelInstallStatus::NotInstalled;
    }
    const auto p = std::filesystem::path(modelsDir_) / item.filename;
    item.localFilePath = p.string();
  }
  return list;
}

core::ModelInstallStatus ModelManager::status(const std::string& modelId) const {
  std::lock_guard lock(mutex_);
  auto it = statuses_.find(modelId);
  if (it != statuses_.end()) {
    return it->second;
  }
  return core::ModelInstallStatus::NotInstalled;
}

std::string ModelManager::modelPath(const std::string& modelId) const {
  std::lock_guard lock(mutex_);
  auto m = manifest_.findModel(modelId);
  if (!m.has_value()) {
    return {};
  }
  return (std::filesystem::path(modelsDir_) / m->filename).string();
}

bool ModelManager::importModel(const std::string& modelId, const std::string& sourcePath) {
  std::error_code ec;
  if (!std::filesystem::exists(sourcePath, ec)) {
    return false;
  }

  std::optional<core::ModelMetadata> meta;
  std::string targetDir;
  {
    std::lock_guard lock(mutex_);
    meta = manifest_.findModel(modelId);
    targetDir = modelsDir_;
  }

  if (!meta.has_value()) {
    return false;
  }

  const auto destPath = std::filesystem::path(targetDir) / meta->filename;
  std::filesystem::create_directories(targetDir, ec);
  std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return false;
  }

  const bool valid = ModelManifest::verifyFile(destPath.string(), meta->sizeBytes, meta->expectedSha256);
  const auto newStatus = valid ? core::ModelInstallStatus::Ready : core::ModelInstallStatus::Corrupted;

  {
    std::lock_guard lock(mutex_);
    statuses_[modelId] = newStatus;
  }

  if (onStatusChanged) {
    onStatusChanged(modelId, newStatus);
  }

  return valid;
}

bool ModelManager::removeModel(const std::string& modelId) {
  std::string targetPath;
  {
    std::lock_guard lock(mutex_);
    auto meta = manifest_.findModel(modelId);
    if (!meta.has_value()) {
      return false;
    }
    targetPath = (std::filesystem::path(modelsDir_) / meta->filename).string();
  }

  std::error_code ec;
  std::filesystem::remove(targetPath, ec);

  {
    std::lock_guard lock(mutex_);
    statuses_[modelId] = core::ModelInstallStatus::NotInstalled;
  }

  if (onStatusChanged) {
    onStatusChanged(modelId, core::ModelInstallStatus::NotInstalled);
  }

  return true;
}

void ModelManager::refresh() {
  std::lock_guard lock(mutex_);
  for (const auto& meta : manifest_.allModels()) {
    const auto targetPath = std::filesystem::path(modelsDir_) / meta.filename;
    std::error_code ec;
    if (std::filesystem::exists(targetPath, ec)) {
      const bool valid = ModelManifest::verifyFile(targetPath.string(), meta.sizeBytes, meta.expectedSha256);
      statuses_[meta.id] = valid ? core::ModelInstallStatus::Ready : core::ModelInstallStatus::Corrupted;
    } else {
      statuses_[meta.id] = core::ModelInstallStatus::NotInstalled;
    }
  }
}

bool ModelManager::checkGpuCompatibility(const std::string& modelId,
                                         const GPUBackend& backend,
                                         int deviceIndex) const {
  std::lock_guard lock(mutex_);
  auto meta = manifest_.findModel(modelId);
  if (!meta.has_value()) {
    return false;
  }

  if (!backend.isAvailable()) {
    return false;
  }

  if (backend.type() == BackendType::Cpu) {
    return true;  // Host CPU fallback is always compatible
  }

  const auto totalMem = backend.totalMemoryBytes(deviceIndex);
  if (totalMem < meta->minVramBytes) {
    return false;
  }

  return true;
}

}  // namespace zyron::ai
