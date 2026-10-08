// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/Common/BaseFileSystem.hpp"

#include <cstdlib>
#include <memory>

namespace zyron::platform {

class WindowsFileSystem final : public BaseFileSystem {
 public:
  ~WindowsFileSystem() override = default;

  [[nodiscard]] std::filesystem::path appDataDir() const override {
    char* val = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&val, &len, "LOCALAPPDATA") == 0 && val != nullptr) {
      const std::filesystem::path p(val);
      std::free(val);
      return p / "ZYRON";
    }
    if (_dupenv_s(&val, &len, "APPDATA") == 0 && val != nullptr) {
      const std::filesystem::path p(val);
      std::free(val);
      return p / "ZYRON";
    }
    return std::filesystem::current_path() / "ZYRON";
  }

  [[nodiscard]] std::filesystem::path defaultMusicDir() const override {
    char* val = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&val, &len, "USERPROFILE") == 0 && val != nullptr) {
      const std::filesystem::path p(val);
      std::free(val);
      return p / "Music";
    }
    return std::filesystem::current_path();
  }
};

}  // namespace zyron::platform

namespace zyron::core {

std::unique_ptr<FileSystem> createPlatformFileSystem() {
  return std::make_unique<platform::WindowsFileSystem>();
}

}  // namespace zyron::core
