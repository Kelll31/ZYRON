// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "Stems/StemTypes.hpp"

namespace zyron::stems {

/// High-performance disk cache for separated 4-stem tracks (SPEC section 42, ADR-0007, ROADMAP P5-05).
/// Keyed by track content hash (SHA-256), separator model ID, and version.
/// Employs optimized binary format (.zyst) with magic header, metadata checksum, and contiguous planar float blocks.
class StemCache {
 public:
  static constexpr std::uint32_t kMagic = 0x5453595A;  // "ZYST"
  static constexpr std::uint32_t kFormatVersion = 1;

  explicit StemCache(std::filesystem::path cacheDirectory);
  ~StemCache() = default;

  [[nodiscard]] const std::filesystem::path& cacheDirectory() const noexcept { return cacheDir_; }

  /// Checks if valid separated stems exist in cache for the specified key.
  [[nodiscard]] bool hasStems(std::string_view contentHash,
                              std::string_view modelName,
                              std::string_view modelVersion) const;

  /// Loads cached 4-stem separation result. Returns nullopt if missing, corrupted, or incompatible.
  [[nodiscard]] std::optional<StemSeparationResult> loadStems(std::string_view contentHash,
                                                              std::string_view modelName,
                                                              std::string_view modelVersion) const;

  /// Atomically writes separated stems to cache using a temporary file and atomic rename.
  bool storeStems(std::string_view contentHash,
                  std::string_view modelName,
                  std::string_view modelVersion,
                  const StemSeparationResult& result);

  /// Deletes cached stem entries associated with contentHash.
  bool invalidate(std::string_view contentHash);

  /// Computes total storage bytes consumed by the stem cache directory.
  [[nodiscard]] std::uint64_t totalCacheSizeBytes() const;

  /// Resolves the filesystem path for a specific cache key.
  [[nodiscard]] std::filesystem::path entryPath(std::string_view contentHash,
                                                std::string_view modelName,
                                                std::string_view modelVersion) const;

 private:
  std::filesystem::path cacheDir_;
};

}  // namespace zyron::stems
