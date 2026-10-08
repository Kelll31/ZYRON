// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace zyron::core {

/// Platform-independent file system interface (SPEC sections 28, 29, 76, 78).
/// Provides standard directory locations, safety validation against directory traversal,
/// audio file detection, and filesystem queries without OS-specific headers.
class FileSystem {
 public:
  virtual ~FileSystem() = default;

  /// Application state/database directory (e.g. %LOCALAPPDATA%/ZYRON, ~/.local/share/zyron).
  [[nodiscard]] virtual std::filesystem::path appDataDir() const = 0;

  /// AI models directory (SPEC section 73, 74).
  [[nodiscard]] virtual std::filesystem::path modelsDir() const = 0;

  /// Cache directory for stems and waveform peaks (SPEC section 42).
  [[nodiscard]] virtual std::filesystem::path cacheDir() const = 0;

  /// Directory where recorded sessions are saved (SPEC section 66).
  [[nodiscard]] virtual std::filesystem::path recordingsDir() const = 0;

  /// User's default music directory (e.g. ~/Music).
  [[nodiscard]] virtual std::filesystem::path defaultMusicDir() const = 0;

  /// Validates that a path is safe and does not escape via malicious traversal (SPEC section 76).
  /// Rejects paths containing null bytes, path traversal sequences that escape their root, etc.
  [[nodiscard]] virtual bool isPathSafe(const std::filesystem::path& path) const = 0;

  /// Checks if a file has an audio extension supported by the application (SPEC section 27).
  [[nodiscard]] virtual bool isAudioFile(const std::filesystem::path& path) const = 0;

  /// Returns true if the file or directory exists.
  [[nodiscard]] virtual bool exists(const std::filesystem::path& path) const = 0;

  /// Returns the file size in bytes, or 0 if unreadable or non-existent.
  [[nodiscard]] virtual std::uint64_t fileSize(const std::filesystem::path& path) const = 0;

  /// Creates all directories along the path if they don't already exist.
  virtual bool createDirectories(const std::filesystem::path& path) const = 0;
};

/// Factory declared in Core, implemented in Platform.
[[nodiscard]] std::unique_ptr<FileSystem> createPlatformFileSystem();

}  // namespace zyron::core
