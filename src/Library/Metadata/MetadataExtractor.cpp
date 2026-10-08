// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Metadata/MetadataExtractor.hpp"

namespace zyron::library {

MetadataExtractor::MetadataExtractor()
    : reader_(std::make_unique<BuiltinMetadataReader>()) {}

MetadataExtractor::MetadataExtractor(std::unique_ptr<IMetadataReader> reader)
    : reader_(std::move(reader)) {
  if (!reader_) {
    reader_ = std::make_unique<BuiltinMetadataReader>();
  }
}

bool MetadataExtractor::extract(const std::filesystem::path& path,
                                TrackMetadata& out,
                                std::string* errorOut) const {
  return reader_->readMetadata(path, out, errorOut);
}

}  // namespace zyron::library
