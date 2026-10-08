// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/AI/ModelTypes.hpp"

namespace zyron::ai {

/// AI model catalog and integrity verification engine (SPEC sections 73, 74, ADR-0013).
class ModelManifest {
 public:
  ModelManifest();
  ~ModelManifest() = default;

  /// Returns standard built-in models catalog (Demucs, Beat This!, S-KEY, ChordMini, MERT).
  [[nodiscard]] static std::vector<core::ModelMetadata> defaultCatalog();

  /// Returns metadata for a model ID, or nullopt if unknown.
  [[nodiscard]] std::optional<core::ModelMetadata> findModel(const std::string& modelId) const;

  /// Registers or updates a model definition in the manifest.
  void registerModel(core::ModelMetadata metadata);

  /// Returns all registered model definitions.
  [[nodiscard]] const std::vector<core::ModelMetadata>& allModels() const noexcept { return catalog_; }

  /// Computes the SHA-256 hex digest of a file on disk.
  [[nodiscard]] static std::string computeSha256(const std::string& filePath);

  /// Validates a model file against expected size and SHA-256 checksum.
  [[nodiscard]] static bool verifyFile(const std::string& filePath,
                                       std::uint64_t expectedSize,
                                       const std::string& expectedSha256);

 private:
  std::vector<core::ModelMetadata> catalog_;
};

}  // namespace zyron::ai
