// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/Release/FirstRunManager.hpp"

#include <chrono>
#include <fstream>
#include <regex>
#include <sstream>

namespace zyron::platform {

namespace {

std::string getCurrentIsoTime() {
  const auto now = std::chrono::system_clock::now();
  const auto timeT = std::chrono::system_clock::to_time_t(now);
  char buffer[32];
#if defined(_WIN32)
  struct tm buf;
  gmtime_s(&buf, &timeT);
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &buf);
#else
  struct tm buf;
  gmtime_r(&timeT, &buf);
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &buf);
#endif
  return std::string(buffer);
}

}  // namespace

FirstRunManager::FirstRunManager(std::string configFilePath,
                                 std::shared_ptr<core::IModelManager> modelManager,
                                 std::shared_ptr<core::IHttpTransport> transport)
    : configFilePath_(std::move(configFilePath)),
      modelManager_(std::move(modelManager)),
      transport_(std::move(transport)) {
  config_.modelsDirectory = "models";
  if (!configFilePath_.empty()) {
    loadConfig();
  }
}

core::FirstRunStep FirstRunManager::currentStep() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return currentStep_;
}

void FirstRunManager::setStep(core::FirstRunStep step) {
  std::lock_guard<std::mutex> lock(mutex_);
  currentStep_ = step;
}

bool FirstRunManager::isFirstRunNeeded() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !config_.isCompleted;
}

const core::FirstRunConfig& FirstRunManager::config() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

void FirstRunManager::setOfflinePolicy(core::OfflinePolicyConsent consent) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.offlineMode = (consent == core::OfflinePolicyConsent::OfflineOnly);
}

bool FirstRunManager::setModelsDirectory(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (path.empty()) return false;

  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) return false;

  config_.modelsDirectory = path;
  return true;
}

std::vector<core::FirstRunModelSummary> FirstRunManager::scanAndVerifyModels() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<core::FirstRunModelSummary> summaries;

  if (modelManager_) {
    modelManager_->refresh();
    const auto models = modelManager_->availableModels();
    for (const auto& m : models) {
      core::FirstRunModelSummary s;
      s.modelId = m.id;
      s.name = m.name;
      s.filename = m.filename;
      s.sizeBytes = m.sizeBytes;
      s.status = modelManager_->status(m.id);
      if (s.status == core::ModelInstallStatus::Ready) {
        s.verificationMessage = "Verified & ready";
      } else if (s.status == core::ModelInstallStatus::Corrupted) {
        s.verificationMessage = "Checksum mismatch or corrupt";
      } else {
        s.verificationMessage = "Not installed";
      }
      summaries.push_back(std::move(s));
    }
  } else {
    // Built-in standard models check against the filesystem directly
    struct BuiltinSpec {
      std::string id;
      std::string name;
      std::string filename;
      std::uint64_t size;
    };
    const std::vector<BuiltinSpec> standard = {
        {"demucs_htdemucs", "Demucs HTDemucs (4-Stem)", "htdemucs.onnx", 85000000},
        {"beat_this", "Beat This! (BPM & Grid)", "beat_this.onnx", 12000000},
        {"chord_mini", "ChordMini (170 Chords)", "chord_mini.onnx", 15000000},
        {"s_key", "S-Key (Camelot / Key)", "s_key.onnx", 8000000},
    };

    const std::filesystem::path dir(config_.modelsDirectory);
    for (const auto& b : standard) {
      core::FirstRunModelSummary s;
      s.modelId = b.id;
      s.name = b.name;
      s.filename = b.filename;
      s.sizeBytes = b.size;

      const auto p = dir / b.filename;
      std::error_code ec;
      if (std::filesystem::exists(p, ec)) {
        const auto actualSize = std::filesystem::file_size(p, ec);
        if (actualSize > 0) {
          s.status = core::ModelInstallStatus::Ready;
          s.verificationMessage = "Present (" + std::to_string(actualSize) + " bytes)";
        } else {
          s.status = core::ModelInstallStatus::Corrupted;
          s.verificationMessage = "Corrupt empty file";
        }
      } else {
        s.status = core::ModelInstallStatus::NotInstalled;
        s.verificationMessage = "Missing file";
      }
      summaries.push_back(std::move(s));
    }
  }

  return summaries;
}

bool FirstRunManager::importModel(const std::string& modelId, const std::string& sourcePath) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sourcePath.empty()) return false;

  std::error_code ec;
  if (!std::filesystem::exists(sourcePath, ec)) return false;

  if (modelManager_) {
    return modelManager_->importModel(modelId, sourcePath);
  }

  const std::filesystem::path src(sourcePath);
  const std::filesystem::path dstDir(config_.modelsDirectory);
  std::filesystem::create_directories(dstDir, ec);
  const auto target = dstDir / src.filename();
  return std::filesystem::copy_file(src, target, std::filesystem::copy_options::overwrite_existing, ec);
}

bool FirstRunManager::downloadModel(const std::string& modelId,
                                    std::function<void(float progress)> onProgress) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Enforce SPEC §75 offline policy
  if (config_.offlineMode) {
    return false;
  }

  if (!transport_) {
    return false;
  }

  const std::string url = "https://models.zyron.audio/" + modelId + ".onnx";
  const std::filesystem::path dstDir(config_.modelsDirectory);
  std::error_code ec;
  std::filesystem::create_directories(dstDir, ec);
  const auto targetPath = (dstDir / (modelId + ".onnx")).string();

  return transport_->downloadFile(url, targetPath, std::move(onProgress));
}

bool FirstRunManager::completeFirstRun() {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.isCompleted = true;
  config_.completedTimestamp = getCurrentIsoTime();
  currentStep_ = core::FirstRunStep::ReadyToLaunch;

  if (!configFilePath_.empty()) {
    saveConfig();
  }
  return true;
}

void FirstRunManager::resetFirstRun() {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.isCompleted = false;
  config_.completedTimestamp.clear();
  currentStep_ = core::FirstRunStep::WelcomeAndPolicy;

  if (!configFilePath_.empty()) {
    saveConfig();
  }
}

bool FirstRunManager::loadConfig() {
  if (configFilePath_.empty()) return false;

  std::ifstream file(configFilePath_);
  if (!file.is_open()) return false;

  std::stringstream ss;
  ss << file.rdbuf();
  const std::string json = ss.str();

  // Extract isCompleted
  const std::regex compRegex(R"delimiter("isCompleted"\s*:\s*(true|false))delimiter");
  std::smatch m;
  if (std::regex_search(json, m, compRegex)) {
    config_.isCompleted = (m[1].str() == "true");
  }

  // Extract offlineMode
  const std::regex offRegex(R"delimiter("offlineMode"\s*:\s*(true|false))delimiter");
  if (std::regex_search(json, m, offRegex)) {
    config_.offlineMode = (m[1].str() == "true");
  }

  // Extract modelsDirectory
  const std::regex dirRegex(R"delimiter("modelsDirectory"\s*:\s*"([^"]*)")delimiter");
  if (std::regex_search(json, m, dirRegex)) {
    config_.modelsDirectory = m[1].str();
  }

  // Extract completedTimestamp
  const std::regex timeRegex(R"delimiter("completedTimestamp"\s*:\s*"([^"]*)")delimiter");
  if (std::regex_search(json, m, timeRegex)) {
    config_.completedTimestamp = m[1].str();
  }

  return true;
}

bool FirstRunManager::saveConfig() const {
  if (configFilePath_.empty()) return false;

  const std::filesystem::path p(configFilePath_);
  std::error_code ec;
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path(), ec);
  }

  std::ofstream file(configFilePath_, std::ios::trunc);
  if (!file.is_open()) return false;

  file << "{\n"
       << "  \"isCompleted\": " << (config_.isCompleted ? "true" : "false") << ",\n"
       << "  \"offlineMode\": " << (config_.offlineMode ? "true" : "false") << ",\n"
       << "  \"modelsDirectory\": \"" << config_.modelsDirectory << "\",\n"
       << "  \"completedTimestamp\": \"" << config_.completedTimestamp << "\"\n"
       << "}\n";

  return true;
}

}  // namespace zyron::platform
