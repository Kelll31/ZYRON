// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/Common/BaseFileSystem.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace zyron::platform {

std::filesystem::path BaseFileSystem::modelsDir() const {
  return appDataDir() / "models";
}

std::filesystem::path BaseFileSystem::cacheDir() const {
  return appDataDir() / "cache";
}

std::filesystem::path BaseFileSystem::recordingsDir() const {
  return appDataDir() / "recordings";
}

bool BaseFileSystem::isPathSafe(const std::filesystem::path& path) const {
  const std::string s = path.generic_string();
  if (s.empty()) {
    return false;
  }

  for (char ch : s) {
    if (ch == '\0' || ch == '\r' || ch == '\n') {
      return false;
    }
  }

  // Prevent path traversal above root or working directory
  const auto normal = path.lexically_normal();
  auto it = normal.begin();
  if (it != normal.end() && *it == "..") {
    return false;
  }

  return true;
}

bool BaseFileSystem::isAudioFile(const std::filesystem::path& path) const {
  std::string ext = path.extension().generic_string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  return ext == ".mp3" || ext == ".wav" || ext == ".wave" || ext == ".flac" || ext == ".aif" || ext == ".aiff" ||
         ext == ".m4a" || ext == ".aac" || ext == ".ogg";
}

bool BaseFileSystem::exists(const std::filesystem::path& path) const {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

std::uint64_t BaseFileSystem::fileSize(const std::filesystem::path& path) const {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  return ec ? 0 : static_cast<std::uint64_t>(size);
}

bool BaseFileSystem::createDirectories(const std::filesystem::path& path) const {
  std::error_code ec;
  return std::filesystem::create_directories(path, ec);
}

}  // namespace zyron::platform
