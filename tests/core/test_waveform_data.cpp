// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>

#include "Core/Audio/DeckTelemetry.hpp"
#include "Core/Audio/WaveformData.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron::core;

TEST_CASE("WaveformData structure and binary persistence", "[core][waveform]") {
  const auto tempPath = std::filesystem::temp_directory_path() / "test_waveform_data.zywv";
  std::error_code ec;
  std::filesystem::remove(tempPath, ec);

  SECTION("Empty WaveformData reports empty and zero duration") {
    WaveformData data;
    CHECK(data.empty());
    CHECK_THAT(data.durationSec(), WithinAbs(0.0, 1e-4));
  }

  SECTION("Save and load binary roundtrip") {
    WaveformData orig;
    orig.sampleRate = 48000;
    orig.channels = 2;
    orig.samplesPerFrame = 256;

    // Add 10 detail frames
    for (int i = 0; i < 10; ++i) {
      WaveformPoint p;
      p.minLeft = -0.5f - static_cast<float>(i) * 0.04f;
      p.maxLeft = 0.5f + static_cast<float>(i) * 0.04f;
      p.minRight = -0.4f;
      p.maxRight = 0.4f;
      p.lowEnergy = 0.8f;
      p.midEnergy = 0.5f;
      p.highEnergy = 0.2f;
      orig.detail.push_back(p);
    }

    // Add 2 overview frames
    for (int i = 0; i < 2; ++i) {
      WaveformPoint p;
      p.minLeft = -0.9f;
      p.maxLeft = 0.9f;
      p.minRight = -0.8f;
      p.maxRight = 0.8f;
      p.lowEnergy = 0.7f;
      p.midEnergy = 0.4f;
      p.highEnergy = 0.1f;
      orig.overview.push_back(p);
    }

    CHECK_FALSE(orig.empty());
    // 10 detail frames * 256 samples / 48000 Hz = 2560 / 48000 = 0.05333 sec
    CHECK_THAT(orig.durationSec(), WithinAbs(2560.0 / 48000.0, 1e-4));

    std::string err;
    REQUIRE(orig.saveToFile(tempPath, &err));

    auto loadedOpt = WaveformData::loadFromFile(tempPath, &err);
    REQUIRE(loadedOpt.has_value());
    const auto& loaded = *loadedOpt;

    CHECK(loaded.sampleRate == 48000);
    CHECK(loaded.channels == 2);
    CHECK(loaded.samplesPerFrame == 256);
    REQUIRE(loaded.detail.size() == 10);
    REQUIRE(loaded.overview.size() == 2);

    CHECK_THAT(loaded.detail[0].minLeft, WithinAbs(-0.5f, 1e-4f));
    CHECK_THAT(loaded.detail[0].maxLeft, WithinAbs(0.5f, 1e-4f));
    CHECK_THAT(loaded.detail[0].lowEnergy, WithinAbs(0.8f, 1e-4f));
    CHECK_THAT(loaded.detail[9].minLeft, WithinAbs(-0.86f, 1e-4f));

    std::filesystem::remove(tempPath, ec);
  }

  SECTION("Load nonexistent file fails gracefully") {
    std::string err;
    auto res = WaveformData::loadFromFile(tempPath, &err);
    CHECK_FALSE(res.has_value());
    CHECK_FALSE(err.empty());
  }
}

TEST_CASE("DeckTelemetry default values and hot cue slots", "[core][telemetry]") {
  DeckTelemetry telem;
  CHECK(telem.deck == DeckId::A);
  CHECK_FALSE(telem.hasTrack);
  CHECK_FALSE(telem.isPlaying);
  CHECK_THAT(telem.playbackSpeed, WithinAbs(1.0, 1e-4));
  CHECK_FALSE(telem.loop.active);

  // Set cue
  CuePointTelemetry cue1;
  cue1.index = 1;
  cue1.frame = 44100;
  cue1.timeSec = 1.0;
  cue1.name = "Drop";
  cue1.color = "0xffff3b30";
  cue1.type = "drop";
  telem.hotCues[0] = cue1;

  REQUIRE(telem.hotCues[0].has_value());
  CHECK(telem.hotCues[0]->name == "Drop");
  CHECK(telem.hotCues[0]->type == "drop");
  CHECK_FALSE(telem.hotCues[1].has_value());
}
