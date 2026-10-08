// SPDX-License-Identifier: AGPL-3.0-only
// SyncManager::alignPhase near the track start, TrackLoader fallback decoder and deferred stem retirement.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/SyncManager.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "Audio/Deck/TrackLoader.hpp"

using namespace zyron::audio;
using namespace zyron::core;

namespace {

constexpr int kRate = 44100;

std::shared_ptr<TrackBuffer> silentTrack(int frames) {
  return std::make_shared<TrackBuffer>(2, frames, static_cast<double>(kRate));
}

std::filesystem::path tempFile(const std::string& name, const std::string& content) {
  const auto dir = std::filesystem::temp_directory_path() / "zyron_test_loader_fallback";
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::ofstream(path, std::ios::binary) << content;
  return path;
}

}  // namespace

TEST_CASE("alignPhase moves forward one beat instead of clamping at frame 0", "[audio][sync][regression]") {
  SyncManager sync;
  DeckPlayer master;
  DeckPlayer target;
  master.prepare(kRate);
  target.prepare(kRate);
  master.loadTrack(silentTrack(kRate * 30));
  target.loadTrack(silentTrack(kRate * 30));

  // 120 BPM -> 22050 frames per beat. The target grid starts before the track, so at frame 1000 its phase is 0.2
  // while the master (at a beat boundary) is at 0.0: the exact fix is -4410 frames, which would go below zero.
  constexpr double kSpb = 22050.0;
  sync.setDeckGrid(DeckId::A, DeckGrid{120.0, 0, kRate, 0});
  sync.setDeckGrid(DeckId::B, DeckGrid{120.0, -3410, kRate, 0});
  master.seek(22050);
  target.seek(1000);
  REQUIRE(sync.getDeckGrid(DeckId::B).beatFraction(1000) == Catch::Approx(0.2).margin(1e-9));

  const std::int64_t adjusted = sync.alignPhase(target, DeckId::B, master, DeckId::A);

  CHECK(adjusted == -4410 + 22050);
  CHECK(target.currentFrame() == 1000 + adjusted);
  CHECK(target.currentFrame() > 0);
  const double phaseError = sync.getDeckGrid(DeckId::B).beatFraction(target.currentFrame());
  const double wrapped = std::min(phaseError, 1.0 - phaseError);
  CHECK(wrapped * kSpb <= 1.0);  // aligned within one sample
}

TEST_CASE("alignPhase steps back normally when there is room before the playhead", "[audio][sync]") {
  SyncManager sync;
  DeckPlayer master;
  DeckPlayer target;
  master.prepare(kRate);
  target.prepare(kRate);
  master.loadTrack(silentTrack(kRate * 30));
  target.loadTrack(silentTrack(kRate * 30));

  sync.setDeckGrid(DeckId::A, DeckGrid{120.0, 0, kRate, 0});
  sync.setDeckGrid(DeckId::B, DeckGrid{120.0, 0, kRate, 0});
  master.seek(22050 * 4);
  target.seek(22050 * 2 + 5000);

  const std::int64_t adjusted = sync.alignPhase(target, DeckId::B, master, DeckId::A);

  CHECK(adjusted == -5000);
  CHECK(target.currentFrame() == 22050 * 2);
}

TEST_CASE("alignPhase ignores a deck without a usable grid", "[audio][sync]") {
  SyncManager sync;
  DeckPlayer master;
  DeckPlayer target;
  master.prepare(kRate);
  target.prepare(kRate);
  master.loadTrack(silentTrack(kRate));
  target.loadTrack(silentTrack(kRate));
  sync.setDeckGrid(DeckId::B, DeckGrid{0.0, 0, kRate, 0});
  target.seek(500);

  CHECK(sync.alignPhase(target, DeckId::B, master, DeckId::A) == 0);
  CHECK(target.currentFrame() == 500);
}

TEST_CASE("TrackLoader uses the fallback decoder when the WAV reader declines", "[audio][loader][fallback]") {
  TrackLoader loader;
  const auto path = tempFile("song.mp3", "not a wav file");
  int calls = 0;
  loader.setFallbackDecoder([&calls](const std::filesystem::path&, std::string*) {
    ++calls;
    return std::make_shared<TrackBuffer>(2, 480, 48000.0);
  });

  std::string error;
  const auto buffer = loader.decodeFile(path, &error);

  REQUIRE(buffer != nullptr);
  CHECK(buffer->numFrames() == 480);
  CHECK(calls == 1);

  DeckPlayer player;
  player.prepare(48000.0);
  const auto result = loader.loadTrackSync(DeckId::A, path, player);
  CHECK(result.success);
  CHECK(player.hasTrack());
  std::filesystem::remove(path);
}

TEST_CASE("TrackLoader reports the fallback decoder's error text", "[audio][loader][fallback]") {
  TrackLoader loader;
  const auto path = tempFile("broken.flac", "garbage");
  loader.setFallbackDecoder([](const std::filesystem::path&, std::string* error) -> std::shared_ptr<TrackBuffer> {
    if (error != nullptr) {
      *error = "flac: truncated frame";
    }
    return nullptr;
  });

  std::string error;
  CHECK(loader.decodeFile(path, &error) == nullptr);
  CHECK(error == "flac: truncated frame");

  DeckPlayer player;
  const auto result = loader.loadTrackSync(DeckId::A, path, player);
  CHECK_FALSE(result.success);
  CHECK(result.errorMessage == "flac: truncated frame");
  CHECK(result.buffer == nullptr);
  CHECK_FALSE(player.hasTrack());
  std::filesystem::remove(path);
}

TEST_CASE("TrackLoader without a fallback reports a non-empty error for unsupported files", "[audio][loader][fallback]") {
  TrackLoader loader;
  const auto path = tempFile("song.ogg", "OggS....");
  std::string error;
  CHECK(loader.decodeFile(path, &error) == nullptr);
  CHECK_FALSE(error.empty());
  CHECK(loader.decodeFile(path, nullptr) == nullptr);  // null error sink is tolerated
  std::filesystem::remove(path);
}

TEST_CASE("TrackLoader keeps retired buffers alive during the grace period", "[audio][loader][retire]") {
  TrackLoader loader;
  auto buffer = silentTrack(100);
  const std::weak_ptr<const TrackBuffer> watcher = buffer;

  loader.retire(buffer);
  buffer.reset();
  REQUIRE(loader.retiredBuffersCount() == 1);

  loader.purgeExpiredBuffers();  // far less than kRetireGrace has passed
  CHECK(loader.retiredBuffersCount() == 1);
  CHECK_FALSE(watcher.expired());

  loader.purgeRetiredBuffers();  // forced purge frees at once
  CHECK(loader.retiredBuffersCount() == 0);
  CHECK(watcher.expired());
}

TEST_CASE("TrackLoader retires stems together with the track on unload", "[audio][loader][retire]") {
  TrackLoader loader;
  DeckPlayer player;
  player.prepare(kRate);
  player.loadTrack(silentTrack(1000));
  std::array<std::shared_ptr<const TrackBuffer>, kStemKindCount> stems;
  for (auto& stem : stems) {
    stem = silentTrack(1000);
  }
  player.loadStems(stems);
  REQUIRE(player.hasStems());
  stems = {};

  loader.unloadTrack(DeckId::A, player);

  CHECK_FALSE(player.hasTrack());
  CHECK_FALSE(player.hasStems());
  CHECK(loader.retiredBuffersCount() == kStemKindCount + 1);
}
