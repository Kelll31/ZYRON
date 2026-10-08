// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <string>

#include "Library/Metadata/TrackMetadata.hpp"

namespace zyron::library {

/// Abstract interface for audio file metadata extraction (SPEC section 29, ADR-0009).
class IMetadataReader {
 public:
  virtual ~IMetadataReader() = default;

  /// Reads metadata and audio stream attributes from an audio file.
  /// Returns true on success, false on unsupported format or parse error.
  [[nodiscard]] virtual bool readMetadata(const std::filesystem::path& path,
                                          TrackMetadata& out,
                                          std::string* errorOut = nullptr) = 0;
};

}  // namespace zyron::library
