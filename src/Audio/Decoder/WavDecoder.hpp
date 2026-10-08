// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Audio/Deck/TrackBuffer.hpp"

namespace zyron::audio {

/// Self-contained, dependency-free RIFF WAV reader and writer (SPEC section 27, ADR-0009).
/// Supports 16-bit PCM, 24-bit PCM, and 32-bit IEEE float formats.
class WavDecoder {
 public:
  /// Decodes a WAV file into a TrackBuffer. Returns nullptr on parse failure or unreadable file.
  [[nodiscard]] static std::shared_ptr<TrackBuffer> decode(const std::filesystem::path& path,
                                                           std::string* errorOut = nullptr);

  /// Encodes a TrackBuffer into a 16-bit or 32-bit float WAV file.
  static bool encode(const std::filesystem::path& path, const TrackBuffer& buffer, bool useFloat32 = true,
                     std::string* errorOut = nullptr);
};

}  // namespace zyron::audio
