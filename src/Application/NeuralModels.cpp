// SPDX-License-Identifier: AGPL-3.0-only
#include "Application/NeuralModels.hpp"

#include <juce_core/juce_core.h>

#include "Library/Hash/ContentHasher.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>
#include <vector>

namespace zyron::application {

namespace {

constexpr const char* kProbeFile = "htdemucs/htdemucs.onnx";  // present in any usable models folder
constexpr int kMaxParentLevels = 8;

/// The weights ZYRON was built and tested with (SHA-256, checked against the Hugging Face LFS hashes on 2026-10-08).
/// A file with any other content is refused: ONNX graphs are code (they can name custom operator libraries), so a
/// models folder found through an environment variable or a parent directory is not trusted on its own.
struct PinnedModel {
  const char* file;
  const char* sha256;
};
constexpr PinnedModel kPinnedModels[] = {
    {"htdemucs/htdemucs.onnx", "68d0bf16428ef66e692cdff8a9ccf28f1ef3f69440d57e58605a4cc55fcc5e74"},
    {"beat-this/beat_this.onnx", "1337dc6c21257ed6b803418efc320df42b5d8eb296ef16648ef60457cd4b9e55"},
    {"beat-this/mel-filterbank.bin", "1ee975d96f44ccf2c3bfe37825c1c1f0b089f5703c7a12a84b1f0a3bce004533"},
    {"skey/skey.onnx", "5113c1378c1007c8559fcb767593366ba9794397b060535eb80a113db50530fc"},
    {"chordmini/chordnet.onnx", "1faee8e1cf168300afe0f47517e76836ac200c3841b0fb430fad7dfea5ad6394"},
};

/// True when `relative` is not pinned (an unknown model) or its content has the pinned hash; fills `error` otherwise.
bool checksumMatches(const std::filesystem::path& path, const std::string& relative, std::string* error) {
  for (const PinnedModel& pinned : kPinnedModels) {
    if (relative != pinned.file) {
      continue;
    }
    const std::string actual = library::ContentHasher::hashFile(path);
    if (actual == pinned.sha256) {
      return true;
    }
    if (error != nullptr) {
      *error = "Refusing to load " + relative + ": its SHA-256 is not the one this ZYRON version expects. Run "
               "scripts\\download_models.ps1 again.";
    }
    return false;
  }
  return true;
}

bool looksLikeModelsDir(const std::filesystem::path& dir) {
  std::error_code ec;
  return std::filesystem::is_directory(dir, ec) &&
         (std::filesystem::exists(dir / kProbeFile, ec) || std::filesystem::exists(dir / "skey" / "skey.onnx", ec) ||
          std::filesystem::exists(dir / "beat-this" / "beat_this.onnx", ec));
}

std::filesystem::path locate(const std::filesystem::path& appDataDir) {
  std::vector<std::filesystem::path> candidates;

  const juce::String fromEnvironment = juce::SystemStats::getEnvironmentVariable("ZYRON_MODELS_DIR", "");
  if (fromEnvironment.isNotEmpty()) {
    candidates.emplace_back(fromEnvironment.toWideCharPointer());
  }
  candidates.push_back(appDataDir / "models");

  const juce::File executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  std::filesystem::path folder(executable.getParentDirectory().getFullPathName().toWideCharPointer());
  for (int level = 0; level < kMaxParentLevels && !folder.empty(); ++level) {
    candidates.push_back(folder / "models");
    const auto parent = folder.parent_path();
    if (parent == folder) {
      break;
    }
    folder = parent;
  }

  for (const auto& candidate : candidates) {
    if (looksLikeModelsDir(candidate)) {
      return candidate;
    }
  }
  return {};
}

}  // namespace

NeuralModels::NeuralModels(const std::filesystem::path& appDataDir) : directory_(locate(appDataDir)) {}

bool NeuralModels::available() const noexcept {
#ifdef ZYRON_HAS_ONNX
  return !directory_.empty();
#else
  return false;
#endif
}

std::shared_ptr<ai::IModelSession> NeuralModels::demucs(std::string* error) {
  return session(demucs_, "htdemucs/htdemucs.onnx", error);
}

std::shared_ptr<ai::IModelSession> NeuralModels::beatThis(std::string* error) {
  return session(beatThis_, "beat-this/beat_this.onnx", error);
}

std::shared_ptr<ai::IModelSession> NeuralModels::skey(std::string* error) {
  return session(skey_, "skey/skey.onnx", error);
}

std::vector<float> NeuralModels::beatThisMelFilterbank(std::string* error) const {
  constexpr std::size_t kFloats = 513 * 128;
  const std::filesystem::path path = directory_ / "beat-this" / "mel-filterbank.bin";
  if (!directory_.empty() && !checksumMatches(path, "beat-this/mel-filterbank.bin", error)) {
    return {};
  }
  std::ifstream in(path, std::ios::binary);
  std::vector<float> filterbank(kFloats);
  if (directory_.empty() || !in.read(reinterpret_cast<char*>(filterbank.data()),
                                     static_cast<std::streamsize>(kFloats * sizeof(float)))) {
    if (error != nullptr) {
      *error = "Cannot read the Beat This! mel filterbank: " + path.string();
    }
    return {};
  }
  return filterbank;
}

std::shared_ptr<ai::IModelSession> NeuralModels::session(std::shared_ptr<ai::IModelSession>& slot,
                                                         const char* relativePath, std::string* error) {
#ifdef ZYRON_HAS_ONNX
  std::lock_guard<std::mutex> lock(mutex_);
  if (slot != nullptr) {
    return slot;
  }
  if (directory_.empty()) {
    if (error != nullptr) {
      *error = "The neural network weights were not found. Run scripts\\download_models.ps1 (or set ZYRON_MODELS_DIR).";
    }
    return nullptr;
  }
  const std::filesystem::path path = directory_ / relativePath;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    if (error != nullptr) {
      *error = "Model file missing: " + path.string();
    }
    return nullptr;
  }
  if (!checksumMatches(path, relativePath, error)) {
    return nullptr;
  }
  const std::u8string utf8 = path.u8string();
  std::unique_ptr<ai::IModelSession> created =
      runtime_.createSession(std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()), cpu_, 0);
  if (created == nullptr) {
    if (error != nullptr) {
      *error = "Could not load " + path.filename().string() + ": " + runtime_.lastError();
    }
    return nullptr;
  }
  slot = std::move(created);
  return slot;
#else
  (void)slot;
  (void)relativePath;
  if (error != nullptr) {
    *error = "This build has no ONNX Runtime";
  }
  return nullptr;
#endif
}

}  // namespace zyron::application
