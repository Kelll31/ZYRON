// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "Audio/Deck/TrackLoader.hpp"
#include "Audio/Decoder/WavDecoder.hpp"
#include "support/AllocationGuard.hpp"
#include "support/AudioTestUtils.hpp"

using namespace zyron::audio;
using namespace zyron::core;
using namespace zyron::test;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path getTempWavPath(const std::string& name) {
  const auto tempDir = std::filesystem::temp_directory_path() / "zyron_test_audio";
  std::filesystem::create_directories(tempDir);
  return tempDir / (name + ".wav");
}

}  // namespace

TEST_CASE("WavDecoder encodes and decodes 32-bit float WAV accurately", "[audio][decoder][wav]") {
  const auto path = getTempWavPath("test_float32");
  constexpr double kRate = 48000.0;
  constexpr std::int64_t kFrames = 2400;  // 50 ms

  TrackBuffer original(2, kFrames, kRate);
  for (std::int64_t i = 0; i < kFrames; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kRate);
    const float sL = std::sin(2.0F * 3.14159265F * 440.0F * t);
    const float sR = std::sin(2.0F * 3.14159265F * 880.0F * t);
    original.channelData(0)[i] = sL;
    original.channelData(1)[i] = sR;
  }

  std::string error;
  const bool encoded = WavDecoder::encode(path, original, true, &error);
  REQUIRE(encoded);

  const auto decoded = WavDecoder::decode(path, &error);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->numChannels() == 2);
  CHECK(decoded->numFrames() == kFrames);
  CHECK(decoded->sampleRate() == kRate);

  // Float roundtrip must be nearly bit-exact
  float maxDiff = 0.0F;
  for (std::int64_t i = 0; i < kFrames; ++i) {
    const float diffL = std::abs(original.sampleAt(0, i) - decoded->sampleAt(0, i));
    const float diffR = std::abs(original.sampleAt(1, i) - decoded->sampleAt(1, i));
    maxDiff = std::max(maxDiff, std::max(diffL, diffR));
  }
  CHECK(maxDiff < 1e-6F);

  std::filesystem::remove(path);
}

TEST_CASE("WavDecoder encodes and decodes 16-bit PCM WAV", "[audio][decoder][wav]") {
  const auto path = getTempWavPath("test_pcm16");
  constexpr double kRate = 44100.0;
  constexpr std::int64_t kFrames = 1000;

  TrackBuffer original(2, kFrames, kRate);
  for (std::int64_t i = 0; i < kFrames; ++i) {
    const float s = 0.5F * std::sin(2.0F * 3.14159265F * 440.0F * static_cast<float>(i) / 44100.0F);
    original.channelData(0)[i] = s;
    original.channelData(1)[i] = s;
  }

  std::string error;
  REQUIRE(WavDecoder::encode(path, original, false, &error));

  const auto decoded = WavDecoder::decode(path, &error);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->numChannels() == 2);
  CHECK(decoded->numFrames() == kFrames);

  // 16-bit quantization noise is <= 1/32768 (~0.00003)
  float maxDiff = 0.0F;
  for (std::int64_t i = 0; i < kFrames; ++i) {
    maxDiff = std::max(maxDiff, std::abs(original.sampleAt(0, i) - decoded->sampleAt(0, i)));
  }
  CHECK(maxDiff < 0.001F);

  std::filesystem::remove(path);
}

TEST_CASE("WavDecoder handles missing and invalid files gracefully", "[audio][decoder][error]") {
  std::string error;
  auto nonExistent = WavDecoder::decode("non_existent_file.wav", &error);
  CHECK(nonExistent == nullptr);
  CHECK_FALSE(error.empty());

  const auto corruptPath = getTempWavPath("corrupt");
  {
    std::ofstream f(corruptPath);
    f << "This is not a RIFF header";
  }
  auto corrupt = WavDecoder::decode(corruptPath, &error);
  CHECK(corrupt == nullptr);
  CHECK_FALSE(error.empty());
  std::filesystem::remove(corruptPath);
}

TEST_CASE("TrackLoader atomic handoff and deferred retirement", "[audio][loader][deferred_free]") {
  TrackLoader loader;
  DeckPlayer player;
  player.prepare(48000.0);

  // Create two temporary test tracks
  const auto pathA = getTempWavPath("track_a");
  const auto pathB = getTempWavPath("track_b");

  TrackBuffer trackA(2, 4800, 48000.0);
  for (int i = 0; i < 4800; ++i) {
    trackA.channelData(0)[i] = 0.4F;
    trackA.channelData(1)[i] = 0.4F;
  }
  REQUIRE(WavDecoder::encode(pathA, trackA, true));

  TrackBuffer trackB(2, 4800, 48000.0);
  for (int i = 0; i < 4800; ++i) {
    trackB.channelData(0)[i] = 0.8F;
    trackB.channelData(1)[i] = 0.8F;
  }
  REQUIRE(WavDecoder::encode(pathB, trackB, true));

  // Load Track A onto DeckPlayer
  const auto resA = loader.loadTrackSync(DeckId::A, pathA, player);
  REQUIRE(resA.success);
  CHECK(player.hasTrack());
  CHECK(loader.retiredBuffersCount() == 0);

  // Start playing Track A
  player.play();

  std::vector<float> outL(256);
  std::vector<float> outR(256);
  float* outChannels[2] = {outL.data(), outR.data()};

  // Render on RT audio thread
  {
    ScopedRealtimeGuard rtGuard;
    for (int b = 0; b < 4; ++b) {
      player.render(outChannels, 2, 256);
    }
  }

  // Load Track B while Track A was active: atomic hand-off + deferred free
  const auto resB = loader.loadTrackSync(DeckId::A, pathB, player);
  REQUIRE(resB.success);

  // Old Track A is in the loader's retirement queue, not freed on audio thread
  CHECK(loader.retiredBuffersCount() == 1);

  // Continue rendering on RT audio thread with Track B: zero allocations!
  player.play();
  {
    ScopedRealtimeGuard rtGuard;
    for (int b = 0; b < 4; ++b) {
      player.render(outChannels, 2, 256);
    }
  }

  // Purge retired buffers on loader thread safely
  loader.purgeRetiredBuffers();
  CHECK(loader.retiredBuffersCount() == 0);

  std::filesystem::remove(pathA);
  std::filesystem::remove(pathB);
}

TEST_CASE("TrackLoader asynchronous loading on worker thread", "[audio][loader][async]") {
  TrackLoader loader;
  DeckPlayer player;
  player.prepare(48000.0);

  const auto path = getTempWavPath("async_test");
  TrackBuffer track(2, 2400, 48000.0);
  for (int i = 0; i < 2400; ++i) {
    track.channelData(0)[i] = 0.5F;
    track.channelData(1)[i] = 0.5F;
  }
  REQUIRE(WavDecoder::encode(path, track, true));

  std::atomic<bool> loaded{false};
  TrackLoadResult asyncResult{};

  loader.loadTrackAsync(DeckId::B, path, player, [&loaded, &asyncResult](const TrackLoadResult& res) {
    asyncResult = res;
    loaded.store(true, std::memory_order_release);
  });

  // Wait for worker thread to complete load
  for (int i = 0; i < 100 && !loaded.load(std::memory_order_acquire); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  REQUIRE(loaded.load());
  CHECK(asyncResult.success);
  CHECK(player.hasTrack());
  CHECK(player.durationSec() > 0.0);

  loader.stop();
  std::filesystem::remove(path);
}
