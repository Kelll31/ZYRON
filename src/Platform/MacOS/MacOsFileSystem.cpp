// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/Common/BaseFileSystem.hpp"

#include <cstdlib>
#include <memory>

namespace zyron::platform {

class MacOsFileSystem final : public BaseFileSystem {
 public:
  ~MacOsFileSystem() override = default;

  [[nodiscard]] std::filesystem::path appDataDir() const override {
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / "Library" / "Application Support" / "ZYRON";
    }
    return std::filesystem::current_path() / "ZYRON";
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
  return std::make_unique<platform::MacOsFileSystem>();
}

}  // namespace zyron::core
