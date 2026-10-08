// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Embedding/EmbeddingPluginService.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace zyron::ai {

EmbeddingPluginService::EmbeddingPluginService()
    : enabled_(false), inferenceCallback_(nullptr) {}

EmbeddingPluginService::EmbeddingPluginService(InferenceCallback callback)
    : enabled_(false), inferenceCallback_(std::move(callback)) {}

core::TrackEmbedding EmbeddingPluginService::extractEmbedding(
    const float* audio,
    std::size_t numSamples,
    int sampleRate,
    core::EmbeddingPluginModel model) const {
  core::TrackEmbedding result;
  result.modelId = std::string(core::embeddingPluginModelName(model));

  // Must be explicitly enabled by user per ADR-0013 policy
  if (!enabled_ || !audio || numSamples == 0 || sampleRate <= 0) {
    return result;
  }

  if (inferenceCallback_) {
    result.vector = inferenceCallback_(audio, numSamples, sampleRate);
    result.dimension = static_cast<int>(result.vector.size());
    return result;
  }

  // Built-in feature embedding (128-dimensional spectral & envelope profile)
  constexpr int kDim = 128;
  result.vector.assign(kDim, 0.0f);
  result.dimension = kDim;

  const std::size_t chunkSize = numSamples / static_cast<std::size_t>(kDim);
  if (chunkSize == 0) return result;

  for (int d = 0; d < kDim; ++d) {
    const std::size_t start = static_cast<std::size_t>(d) * chunkSize;
    double sumSq = 0.0;
    for (std::size_t i = 0; i < chunkSize && (start + i) < numSamples; ++i) {
      const double s = audio[start + i];
      sumSq += s * s;
    }
    result.vector[static_cast<std::size_t>(d)] = static_cast<float>(std::sqrt(sumSq / static_cast<double>(chunkSize)));
  }

  // Normalize to unit length
  double normSq = 0.0;
  for (float v : result.vector) normSq += static_cast<double>(v * v);
  const double norm = std::sqrt(normSq);
  if (norm > 1e-9) {
    for (float& v : result.vector) v = static_cast<float>(static_cast<double>(v) / norm);
  }

  return result;
}

float EmbeddingPluginService::computeSimilarity(
    const core::TrackEmbedding& a,
    const core::TrackEmbedding& b) const noexcept {
  if (!enabled_) {
    return 0.0f;
  }
  return core::cosineSimilarity(a.vector, b.vector);
}

std::vector<SimilaritySearchResult> EmbeddingPluginService::findSimilarTracks(
    const core::TrackEmbedding& query,
    const std::vector<std::pair<std::string, core::TrackEmbedding>>& catalog,
    std::size_t topK) const {
  std::vector<SimilaritySearchResult> results;
  if (!enabled_ || query.empty() || catalog.empty()) {
    return results;
  }

  for (const auto& [trackId, emb] : catalog) {
    if (emb.empty()) continue;
    const float sim = computeSimilarity(query, emb);
    results.push_back({trackId, sim});
  }

  std::sort(results.begin(), results.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.score > rhs.score;
  });

  if (results.size() > topK) {
    results.resize(topK);
  }

  return results;
}

}  // namespace zyron::ai
