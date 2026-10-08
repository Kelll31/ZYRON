// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

#include "Core/System/FileSystem.hpp"

namespace zyron::platform {

/// Base implementation of FileSystem providing OS-agnostic operations (SPEC section 76, 78).
class BaseFileSystem : public core::FileSystem {
 public:
  ~BaseFileSystem() override = default;

  [[nodiscard]] std::filesystem::path modelsDir() const override;
  [[nodiscard]] std::filesystem::path cacheDir() const override;
  [[nodiscard]] std::filesystem::path recordingsDir() const override;

  [[nodiscard]] bool isPathSafe(const std::filesystem::path& path) const override;
  [[nodiscard]] bool isAudioFile(const std::filesystem::path& path) const override;
  [[nodiscard]] bool exists(const std::filesystem::path& path) const override;
  [[nodiscard]] std::uint64_t fileSize(const std::filesystem::path& path) const override;
  bool createDirectories(const std::filesystem::path& path) const override;
};

}  // namespace zyron::platform
