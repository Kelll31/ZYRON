// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/Common/BaseFileSystem.hpp"

#include <cstdlib>
#include <memory>

namespace zyron::platform {

class LinuxFileSystem final : public BaseFileSystem {
 public:
  ~LinuxFileSystem() override = default;

  [[nodiscard]] std::filesystem::path appDataDir() const override {
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    if (xdgData != nullptr && *xdgData != '\0') {
      return std::filesystem::path(xdgData) / "zyron";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / ".local" / "share" / "zyron";
    }
    return std::filesystem::current_path() / "zyron";
  }

  [[nodiscard]] std::filesystem::path cacheDir() const override {
    const char* xdgCache = std::getenv("XDG_CACHE_HOME");
    if (xdgCache != nullptr && *xdgCache != '\0') {
      return std::filesystem::path(xdgCache) / "zyron";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / ".cache" / "zyron";
    }
    return BaseFileSystem::cacheDir();
  }

  [[nodiscard]] std::filesystem::path defaultMusicDir() const override {
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / "Music";
    }
    return std::filesystem::current_path();
  }
};

}  // namespace zyron::platform

namespace zyron::core {

std::unique_ptr<FileSystem> createPlatformFileSystem() {
  return std::make_unique<platform::LinuxFileSystem>();
}

}  // namespace zyron::core
