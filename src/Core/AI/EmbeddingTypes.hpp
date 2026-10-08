// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zyron::core {

/// Types of optional embedding models supported as plug-ins (ADR-0013, docs/AI_MODELS.md, ROADMAP P6-05).
enum class EmbeddingPluginModel : std::uint8_t {
  MertV2 = 0,   // MERT-v2-FullSong (632M params, 1024-d, CC BY-NC 4.0)
  MuQLarge,     // OpenMuQ/MuQ-large (music-text multimodal, CC BY-NC 4.0)
  LaionClap     // LAION-CLAP (audio-text zero-shot)
};

[[nodiscard]] constexpr std::string_view embeddingPluginModelName(EmbeddingPluginModel m) noexcept {
  switch (m) {
    case EmbeddingPluginModel::MertV2: return "MERT-v2";
    case EmbeddingPluginModel::MuQLarge: return "MuQ-large";
    case EmbeddingPluginModel::LaionClap: return "LAION-CLAP";
  }
  return "Unknown";
}

/// Representation of an extracted vector embedding for a track.
struct TrackEmbedding {
  std::string modelId;
  int dimension{0};
  std::vector<float> vector;

  [[nodiscard]] bool empty() const noexcept { return vector.empty(); }
};

/// Computes cosine similarity between two vector embeddings in range -1.0 .. 1.0 (typically 0.0 .. 1.0 for music).
[[nodiscard]] inline float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) noexcept {
  if (a.empty() || b.empty() || a.size() != b.size()) {
    return 0.0f;
  }

  double dot = 0.0;
  double normA = 0.0;
  double normB = 0.0;

  for (std::size_t i = 0; i < a.size(); ++i) {
    dot += static_cast<double>(a[i] * b[i]);
    normA += static_cast<double>(a[i] * a[i]);
    normB += static_cast<double>(b[i] * b[i]);
  }

  if (normA <= 1e-12 || normB <= 1e-12) {
    return 0.0f;
  }

  return static_cast<float>(dot / (std::sqrt(normA) * std::sqrt(normB)));
}

}  // namespace zyron::core
