// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Library/Metadata/BuiltinMetadataReader.hpp"
#include "Library/Metadata/IMetadataReader.hpp"
#include "Library/Metadata/TrackMetadata.hpp"

namespace zyron::library {

/// Facade for audio file metadata extraction with pluggable backend readers (SPEC section 29).
class MetadataExtractor {
 public:
  /// Creates an extractor using the default BuiltinMetadataReader.
  MetadataExtractor();

  /// Creates an extractor with a custom reader (e.g. FFmpeg or test mock).
  explicit MetadataExtractor(std::unique_ptr<IMetadataReader> reader);

  ~MetadataExtractor() = default;

  /// Extracts metadata from an audio file.
  [[nodiscard]] bool extract(const std::filesystem::path& path,
                             TrackMetadata& out,
                             std::string* errorOut = nullptr) const;

 private:
  std::unique_ptr<IMetadataReader> reader_;
};

}  // namespace zyron::library
