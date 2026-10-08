// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>
#include <vector>

#include "Audio/Decoder/WavDecoder.hpp"
#include "Recording/MasterRecorder.hpp"
#include "Recording/WavFileWriter.hpp"
#include "support/AllocationGuard.hpp"

using namespace zyron::recording;
using namespace zyron::audio;
using namespace zyron::test;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path getTempRecordPath(const std::string& name) {
  const auto tempDir = std::filesystem::temp_directory_path() / "zyron_rec_test";
  std::filesystem::create_directories(tempDir);
  return tempDir / (name + ".wav");
}

}  // namespace

TEST_CASE("MasterRecorder writeSamples is strictly realtime safe", "[recording][rt]") {
  MasterRecorder recorder;
  const auto path = getTempRecordPath("rt_safe_rec");

  REQUIRE(recorder.start(path, 48000.0, 2, true));

  std::vector<float> left(256, 0.5F);
  std::vector<float> right(256, -0.5F);
  const float* channels[2] = {left.data(), right.data()};

  {
    ScopedRealtimeGuard rtGuard;
    recorder.writeSamples(channels, 2, 256);
  }

  recorder.stop();
  std::filesystem::remove(path);
}

TEST_CASE("WavFileWriter incremental writing and header finalization", "[recording][writer]") {
  WavFileWriter writer;
  const auto path = getTempRecordPath("writer_test");

  std::string error;
  REQUIRE(writer.open(path, 2, 48000.0, true, &error));
  CHECK(writer.isOpen());

  // Write 1000 frames of stereo audio
  std::vector<float> frames(2000, 0.75F);
  REQUIRE(writer.writeFrames(frames.data(), 1000));
  CHECK(writer.framesWritten() == 1000);

  writer.close();
  CHECK_FALSE(writer.isOpen());

  // Verify file on disk via WavDecoder
  auto decoded = WavDecoder::decode(path);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->numChannels() == 2);
  CHECK(decoded->numFrames() == 1000);
  CHECK_THAT(decoded->sampleAt(0, 500), WithinAbs(0.75F, 1e-5F));
  CHECK_THAT(decoded->sampleAt(1, 500), WithinAbs(0.75F, 1e-5F));

  std::filesystem::remove(path);
}

TEST_CASE("MasterRecorder full record, pause, resume and flush lifecycle", "[recording][lifecycle]") {
  MasterRecorder recorder;
  const auto path = getTempRecordPath("lifecycle_test");

  constexpr double kRate = 48000.0;
  REQUIRE(recorder.start(path, kRate, 2, true));
  CHECK(recorder.isRecording());
  CHECK_FALSE(recorder.isPaused());

  // 1. Record 2400 frames of signal (0.4)
  std::vector<float> sigA_L(256, 0.4F);
  std::vector<float> sigA_R(256, 0.4F);
  const float* chA[2] = {sigA_L.data(), sigA_R.data()};

  for (int b = 0; b < 10; ++b) {  // 10 * 256 = 2560 frames
    recorder.writeSamples(chA, 2, 256);
  }

  // 2. Pause recording
  recorder.pause();
  CHECK(recorder.isPaused());

  // Feed 10 blocks while paused (should NOT be recorded)
  std::vector<float> sigIgnore(256, 0.9F);
  const float* chIgn[2] = {sigIgnore.data(), sigIgnore.data()};
  for (int b = 0; b < 10; ++b) {
    recorder.writeSamples(chIgn, 2, 256);
  }

  // 3. Resume recording
  recorder.resume();
  CHECK(recorder.isRecording());
  CHECK_FALSE(recorder.isPaused());

  // 4. Record 2560 frames of signal (0.8)
  std::vector<float> sigB_L(256, 0.8F);
  std::vector<float> sigB_R(256, 0.8F);
  const float* chB[2] = {sigB_L.data(), sigB_R.data()};

  for (int b = 0; b < 10; ++b) {  // 10 * 256 = 2560 frames
    recorder.writeSamples(chB, 2, 256);
  }

  // Give writer thread a moment to drain
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // 5. Stop recording and finalize
  recorder.stop();
  CHECK_FALSE(recorder.isRecording());
  CHECK(recorder.overflowDropsCount() == 0);

  // Total recorded frames should be exactly 2560 + 2560 = 5120 frames
  CHECK(recorder.recordedFrames() == 5120);
  CHECK_THAT(recorder.recordedDurationSec(), WithinAbs(5120.0 / kRate, 1e-4));

  // Read back and check contents
  auto decoded = WavDecoder::decode(path);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->numFrames() == 5120);

  // First part was 0.4
  CHECK_THAT(decoded->sampleAt(0, 100), WithinAbs(0.4F, 1e-5F));
  // Second part was 0.8
  CHECK_THAT(decoded->sampleAt(0, 3000), WithinAbs(0.8F, 1e-5F));

  std::filesystem::remove(path);
}

TEST_CASE("MasterRecorder handles FIFO overflow without blocking audio thread", "[recording][overflow]") {
  MasterRecorder recorder;
  const auto path = getTempRecordPath("overflow_test");

  REQUIRE(recorder.start(path, 48000.0, 2, true));

  // Try to write more samples than FIFO capacity at once (e.g. 70,000 frames)
  constexpr int kBigBlock = 35000;
  std::vector<float> bigL(kBigBlock, 0.2F);
  std::vector<float> bigR(kBigBlock, 0.2F);
  const float* ch[2] = {bigL.data(), bigR.data()};

  // Push twice rapidly (70,000 frames * 2 channels = 140,000 floats > 65536 capacity)
  recorder.writeSamples(ch, 2, kBigBlock);
  recorder.writeSamples(ch, 2, kBigBlock);

  // Audio thread did not block or crash, overflow was counted
  CHECK(recorder.overflowDropsCount() > 0);

  recorder.stop();
  std::filesystem::remove(path);
}
