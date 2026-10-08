// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

#include "Stems/Cache/StemCache.hpp"
#include "Stems/StemTypes.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron;

namespace {

stems::StemSeparationResult createTestStemResult(double sampleRate, std::int64_t numFrames) {
  stems::StemSeparationResult res;
  res.success = true;
  res.sampleRate = sampleRate;
  res.numFrames = numFrames;

  const auto frames = static_cast<std::size_t>(numFrames);
  for (std::size_t s = 0; s < stems::kStemCount; ++s) {
    auto& buf = res.stems[s];
    buf.sampleRate = sampleRate;
    buf.channels = 2;
    buf.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
      buf.left[i] = static_cast<float>(s + 1) * 0.1f + static_cast<float>(i) * 0.0001f;
      buf.right[i] = -buf.left[i];
    }
  }
  return res;
}

}  // namespace

TEST_CASE("StemCache binary persistence, retrieval, and invalidation", "[stems][cache]") {
  const auto tempDir = std::filesystem::temp_directory_path() / "zyron_stem_cache_test";
  std::error_code ec;
  std::filesystem::remove_all(tempDir, ec);

  stems::StemCache cache(tempDir);

  const std::string contentHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  const std::string modelName = "htdemucs";
  const std::string modelVersion = "v4";

  SECTION("hasStems is initially false for missing entry") {
    CHECK_FALSE(cache.hasStems(contentHash, modelName, modelVersion));
    CHECK_FALSE(cache.loadStems(contentHash, modelName, modelVersion).has_value());
  }

  SECTION("storeStems persists result and loadStems roundtrips data exactly") {
    const auto orig = createTestStemResult(44100.0, 1024);
    REQUIRE(cache.storeStems(contentHash, modelName, modelVersion, orig));

    CHECK(cache.hasStems(contentHash, modelName, modelVersion));
    CHECK(cache.totalCacheSizeBytes() > 0);

    const auto loadedOpt = cache.loadStems(contentHash, modelName, modelVersion);
    REQUIRE(loadedOpt.has_value());

    const auto& loaded = *loadedOpt;
    CHECK(loaded.success);
    CHECK(loaded.sampleRate == orig.sampleRate);
    CHECK(loaded.numFrames == orig.numFrames);

    for (std::size_t s = 0; s < stems::kStemCount; ++s) {
      const auto& origBuf = orig.stems[s];
      const auto& loadedBuf = loaded.stems[s];
      REQUIRE(loadedBuf.numFrames() == origBuf.numFrames());

      for (std::size_t i = 0; i < origBuf.numFrames(); ++i) {
        CHECK_THAT(loadedBuf.left[i], WithinAbs(origBuf.left[i], 1e-6f));
        CHECK_THAT(loadedBuf.right[i], WithinAbs(origBuf.right[i], 1e-6f));
      }
    }
  }

  SECTION("Different models or versions do not collide") {
    const auto orig1 = createTestStemResult(44100.0, 512);
    const auto orig2 = createTestStemResult(44100.0, 256);

    REQUIRE(cache.storeStems(contentHash, "htdemucs", "v4", orig1));
    REQUIRE(cache.storeStems(contentHash, "bs_roformer", "v1", orig2));

    const auto loaded1 = cache.loadStems(contentHash, "htdemucs", "v4");
    const auto loaded2 = cache.loadStems(contentHash, "bs_roformer", "v1");

    REQUIRE(loaded1.has_value());
    REQUIRE(loaded2.has_value());
    CHECK(loaded1->numFrames == 512);
    CHECK(loaded2->numFrames == 256);
  }

  SECTION("invalidate deletes cached entries matching hash") {
    const auto orig = createTestStemResult(44100.0, 512);
    REQUIRE(cache.storeStems(contentHash, modelName, modelVersion, orig));
    CHECK(cache.hasStems(contentHash, modelName, modelVersion));

    CHECK(cache.invalidate(contentHash));
    CHECK_FALSE(cache.hasStems(contentHash, modelName, modelVersion));
    CHECK_FALSE(cache.loadStems(contentHash, modelName, modelVersion).has_value());
  }

  SECTION("Corrupted or truncated file fails gracefully") {
    const auto p = cache.entryPath(contentHash, "corrupt", "v1");
    {
      std::ofstream out(p, std::ios::binary);
      const std::uint32_t badMagic = 0x12345678;
      out.write(reinterpret_cast<const char*>(&badMagic), sizeof(badMagic));
    }

    CHECK_FALSE(cache.loadStems(contentHash, "corrupt", "v1").has_value());
  }

  std::filesystem::remove_all(tempDir, ec);
}
