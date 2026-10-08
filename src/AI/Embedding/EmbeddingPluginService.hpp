// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/AI/EmbeddingTypes.hpp"
#include "Core/AI/ModelTypes.hpp"

namespace zyron::ai {

/// Result of a nearest-neighbor similarity search using track embeddings.
struct SimilaritySearchResult {
  std::string trackId;
  float score{0.0f};  // Cosine similarity: 0.0 .. 1.0
};

/// Service managing optional NC-licensed embedding model plugins (ADR-0013, SPEC section 74, ROADMAP P6-05).
/// Off by default to guarantee the core DJ workflow functions completely under permissive licenses.
class EmbeddingPluginService {
 public:
  /// Inference callback type for embedding extraction:
  /// Input: audio buffer [numSamples]
  /// Output: vector embedding [dimension]
  using InferenceCallback = std::function<std::vector<float>(
      const float* audio, std::size_t numSamples, int sampleRate)>;

  EmbeddingPluginService();
  explicit EmbeddingPluginService(InferenceCallback callback);
  ~EmbeddingPluginService() = default;

  /// Returns true if the user explicitly enabled optional embedding plugins.
  [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

  /// Enables or disables optional embedding plugins.
  void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

  /// Returns the mandatory non-commercial license warning notice (ADR-0013).
  [[nodiscard]] static std::string_view nonCommercialLicenseNotice() noexcept {
    return "MERT-v2 and MuQ weights are licensed under CC BY-NC 4.0 (Non-Commercial). "
           "They are optional plugins and not bundled with ZYRON. Commercial use requires "
           "disabling these models.";
  }

  /// Extracts embedding vector for audio. Returns empty TrackEmbedding if plugin is disabled.
  [[nodiscard]] core::TrackEmbedding extractEmbedding(
      const float* audio,
      std::size_t numSamples,
      int sampleRate,
      core::EmbeddingPluginModel model = core::EmbeddingPluginModel::MertV2) const;

  /// Computes cosine similarity between two track embeddings. Returns 0.0 if plugin is disabled.
  [[nodiscard]] float computeSimilarity(
      const core::TrackEmbedding& a,
      const core::TrackEmbedding& b) const noexcept;

  /// Performs vector similarity search across a catalog of track embeddings.
  [[nodiscard]] std::vector<SimilaritySearchResult> findSimilarTracks(
      const core::TrackEmbedding& query,
      const std::vector<std::pair<std::string, core::TrackEmbedding>>& catalog,
      std::size_t topK = 5) const;

 private:
  bool enabled_{false};  // OFF by default per ADR-0013
  InferenceCallback inferenceCallback_{nullptr};
};

}  // namespace zyron::ai
