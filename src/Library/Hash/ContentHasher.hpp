// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace zyron::library {

/// Standard SHA-256 content hashing for audio track identity (SPEC section 28, ARCHITECTURE section 9).
/// Tracks are identified by their content hash so moved or renamed files retain their hot cues,
/// beatgrids, and stem separation cache.
class ContentHasher {
 public:
  /// Computes the SHA-256 hash of a file as a 64-character lowercase hexadecimal string.
  /// Reads in streaming chunks (64 KB) to avoid loading large audio files into memory.
  /// Returns an empty string and sets errorOut on file read failure.
  [[nodiscard]] static std::string hashFile(const std::filesystem::path& path,
                                            std::string* errorOut = nullptr);

  /// Computes the SHA-256 hash of an in-memory byte buffer.
  [[nodiscard]] static std::string hashBytes(const void* data, std::size_t size);

  /// Computes the SHA-256 hash of a string.
  [[nodiscard]] static std::string hashString(std::string_view str);
};

}  // namespace zyron::library
