// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "AI/Embedding/EmbeddingPluginService.hpp"
#include "Core/AI/EmbeddingTypes.hpp"

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::vector<float> generateSineWave(float freqHz, int sampleRate, double durationSec) {
  const std::size_t numSamples = static_cast<std::size_t>(durationSec * sampleRate);
  std::vector<float> audio(numSamples, 0.0f);
  for (std::size_t i = 0; i < numSamples; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
    audio[i] = std::sin(2.0f * kPi * freqHz * static_cast<float>(t));
  }
  return audio;
}

}  // namespace

TEST_CASE("EmbeddingPluginService: default-off policy and licensing notices (ADR-0013, P6-05)", "[ai][embedding]") {
  zyron::ai::EmbeddingPluginService service;

  // 1. Must be disabled by default (ADR-0013)
  CHECK_FALSE(service.isEnabled());

  // 2. Mandatory non-commercial license notice
  const auto notice = zyron::ai::EmbeddingPluginService::nonCommercialLicenseNotice();
  CHECK_FALSE(notice.empty());
  CHECK(notice.find("CC BY-NC 4.0") != std::string_view::npos);

  // 3. When disabled, extraction and similarity return safe empty/zero values
  const int sampleRate = 44100;
  const auto audio = generateSineWave(440.0f, sampleRate, 2.0);
  const auto emb = service.extractEmbedding(audio.data(), audio.size(), sampleRate);
  CHECK(emb.empty());

  zyron::core::TrackEmbedding a;
  a.vector = {1.0f, 0.0f};
  zyron::core::TrackEmbedding b;
  b.vector = {1.0f, 0.0f};
  CHECK(service.computeSimilarity(a, b) == 0.0f);

  const auto similar = service.findSimilarTracks(a, {{"track1", b}});
  CHECK(similar.empty());
}

TEST_CASE("EmbeddingPluginService: enabled embedding extraction and cosine similarity", "[ai][embedding]") {
  zyron::ai::EmbeddingPluginService service;
  service.setEnabled(true);
  REQUIRE(service.isEnabled());

  const int sampleRate = 44100;
  const auto audioA = generateSineWave(220.0f, sampleRate, 2.0);
  const auto audioB = generateSineWave(880.0f, sampleRate, 2.0);

  const auto embA = service.extractEmbedding(audioA.data(), audioA.size(), sampleRate);
  const auto embA_again = service.extractEmbedding(audioA.data(), audioA.size(), sampleRate);
  const auto embB = service.extractEmbedding(audioB.data(), audioB.size(), sampleRate);

  REQUIRE_FALSE(embA.empty());
  REQUIRE(embA.dimension > 0);

  // Check unit normalization
  double normSq = 0.0;
  for (float v : embA.vector) normSq += static_cast<double>(v * v);
  CHECK(std::abs(std::sqrt(normSq) - 1.0) < 1e-4);

  // Self-similarity must be 1.0
  const float simSelf = service.computeSimilarity(embA, embA_again);
  CHECK(std::abs(simSelf - 1.0f) < 1e-4f);

  // Cross-similarity between different frequencies should be less
  const float simCross = service.computeSimilarity(embA, embB);
  CHECK(simCross < simSelf);
}

TEST_CASE("EmbeddingPluginService: custom inference callback and nearest-neighbor search", "[ai][embedding]") {
  bool callbackInvoked = false;

  auto mockCallback = [&](const float* /*audio*/, std::size_t /*numSamples*/, int /*sampleRate*/) {
    callbackInvoked = true;
    std::vector<float> vec(1024, 0.0f);
    vec[0] = 1.0f;  // Unit vector along dimension 0
    return vec;
  };

  zyron::ai::EmbeddingPluginService service(mockCallback);
  service.setEnabled(true);

  const int sampleRate = 44100;
  const auto audio = generateSineWave(440.0f, sampleRate, 1.0);
  const auto emb = service.extractEmbedding(audio.data(), audio.size(), sampleRate);

  CHECK(callbackInvoked);
  REQUIRE(emb.dimension == 1024);
  CHECK(emb.vector[0] == 1.0f);

  // Test catalog search
  zyron::core::TrackEmbedding closeMatch;
  closeMatch.vector.assign(1024, 0.0f);
  closeMatch.vector[0] = 0.95f;
  closeMatch.vector[1] = 0.05f;

  zyron::core::TrackEmbedding distantMatch;
  distantMatch.vector.assign(1024, 0.0f);
  distantMatch.vector[100] = 1.0f;

  std::vector<std::pair<std::string, zyron::core::TrackEmbedding>> catalog = {
      {"track_distant", distantMatch},
      {"track_close", closeMatch}
  };

  const auto searchResults = service.findSimilarTracks(emb, catalog, 5);
  REQUIRE(searchResults.size() == 2);
  CHECK(searchResults[0].trackId == "track_close");
  CHECK(searchResults[0].score > searchResults[1].score);
  CHECK(searchResults[0].score > 0.9f);
}
