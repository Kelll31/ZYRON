// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

#include "Analysis/Bpm/BeatgridEditor.hpp"

using namespace zyron::analysis;

TEST_CASE("BeatgridEditor basic state and editing operations", "[analysis][beatgrid]") {
  BeatgridData data;
  data.bpm = 174.0;
  data.firstBeatFrame = 4410;
  data.sampleRate = 44100;
  data.downbeatOffset = 0;
  data.source = "auto";

  BeatgridEditor editor(data);

  SECTION("initializes with provided data and auto source") {
    CHECK(editor.bpm() == 174.0);
    CHECK(editor.firstBeatFrame() == 4410);
    CHECK(editor.sampleRate() == 44100);
    CHECK(editor.downbeatOffset() == 0);
    CHECK_FALSE(editor.isUserModified());
    CHECK(editor.source() == "auto");
  }

  SECTION("setBpm modifies BPM and marks source as user (§16)") {
    editor.setBpm(175.0);
    CHECK(editor.bpm() == 175.0);
    CHECK(editor.isUserModified());
    CHECK(editor.source() == "user");
  }

  SECTION("adjustBpm increments or decrements BPM") {
    editor.adjustBpm(0.5);
    CHECK_THAT(editor.bpm(), Catch::Matchers::WithinRel(174.5, 1e-4));
    CHECK(editor.isUserModified());

    editor.adjustBpm(-1.0);
    CHECK_THAT(editor.bpm(), Catch::Matchers::WithinRel(173.5, 1e-4));
  }

  SECTION("setFirstBeat updates anchor frame and marks source as user") {
    editor.setFirstBeat(8820);
    CHECK(editor.firstBeatFrame() == 8820);
    CHECK(editor.isUserModified());
  }

  SECTION("shiftPhase shifts grid forward and backward") {
    editor.shiftPhase(200);
    CHECK(editor.firstBeatFrame() == 4610);
    CHECK(editor.isUserModified());

    editor.shiftPhase(-100);
    CHECK(editor.firstBeatFrame() == 4510);
  }

  SECTION("shiftPhaseMs shifts grid accurately by millisecond offset") {
    // 10 ms at 44100 Hz = 441 samples
    editor.shiftPhaseMs(10.0);
    CHECK(editor.firstBeatFrame() == 4410 + 441);
    CHECK(editor.isUserModified());
  }

  SECTION("setDownbeatOffset updates bar start offset modulo 4") {
    editor.setDownbeatOffset(3);
    CHECK(editor.downbeatOffset() == 3);
    CHECK(editor.isUserModified());

    editor.setDownbeatOffset(6);  // 6 % 4 == 2
    CHECK(editor.downbeatOffset() == 2);
  }
}

TEST_CASE("BeatgridEditor tap tempo", "[analysis][beatgrid]") {
  BeatgridData data;
  data.bpm = 120.0;
  data.sampleRate = 44100;
  BeatgridEditor editor(data);

  // Simulate tapping at 174 BPM: interval = 44100 * 60 / 174 = 15206.89 samples
  constexpr std::int64_t interval = 15207;
  std::int64_t currentSample = 10000;

  editor.tapTempo(currentSample);
  CHECK(editor.bpm() == 120.0);  // 1 tap is not enough

  currentSample += interval;
  editor.tapTempo(currentSample);
  CHECK(editor.bpm() == 120.0);  // 2 taps not enough

  currentSample += interval;
  editor.tapTempo(currentSample);
  CHECK(editor.bpm() == 120.0);  // 3 taps not enough

  currentSample += interval;
  editor.tapTempo(currentSample);
  // 4 taps give 3 intervals -> updates BPM
  CHECK_THAT(editor.bpm(), Catch::Matchers::WithinAbs(174.0, 0.1));
  CHECK(editor.isUserModified());

  SECTION("timeout resets tap tempo history") {
    // More than 2.5 seconds later (44100 * 2.5 = 110250)
    currentSample += 150000;
    editor.tapTempo(currentSample);
    // Should reset and not update BPM immediately
    CHECK_THAT(editor.bpm(), Catch::Matchers::WithinAbs(174.0, 0.1));
  }
}

TEST_CASE("BeatgridEditor quantization and navigation queries", "[analysis][beatgrid]") {
  BeatgridData data;
  data.bpm = 120.0;
  data.firstBeatFrame = 0;
  data.sampleRate = 44100;
  data.downbeatOffset = 0;
  // 120 BPM at 44100 Hz = 22050 samples per beat
  BeatgridEditor editor(data);

  SECTION("findNearestBeat finds closest grid line") {
    CHECK(editor.findNearestBeat(0) == 0);
    CHECK(editor.findNearestBeat(10000) == 0);
    CHECK(editor.findNearestBeat(12000) == 22050);
    CHECK(editor.findNearestBeat(22050) == 22050);
    CHECK(editor.findNearestBeat(33075) == 44100);
  }

  SECTION("findNextBeat and findPreviousBeat") {
    CHECK(editor.findNextBeat(0) == 22050);
    CHECK(editor.findNextBeat(10000) == 22050);
    CHECK(editor.findNextBeat(22050) == 44100);

    CHECK(editor.findPreviousBeat(22050) == 22050);
    CHECK(editor.findPreviousBeat(22049) == 0);
    CHECK(editor.findPreviousBeat(10000) == 0);
  }

  SECTION("getBeatFraction calculates sub-beat phase") {
    CHECK_THAT(editor.getBeatFraction(0), Catch::Matchers::WithinAbs(0.0, 1e-4));
    CHECK_THAT(editor.getBeatFraction(11025), Catch::Matchers::WithinAbs(0.5, 1e-4));
    CHECK_THAT(editor.getBeatFraction(5512), Catch::Matchers::WithinAbs(0.25, 1e-2));
  }

  SECTION("getBarBeat reports musical bar and beat-in-bar") {
    auto pos0 = editor.getBarBeat(0);
    CHECK(pos0.barIndex == 0);
    CHECK(pos0.beatInBar == 1);

    auto pos1 = editor.getBarBeat(22050);
    CHECK(pos1.barIndex == 0);
    CHECK(pos1.beatInBar == 2);

    auto pos4 = editor.getBarBeat(88200);  // 4 beats later
    CHECK(pos4.barIndex == 1);
    CHECK(pos4.beatInBar == 1);
  }
}

TEST_CASE("BeatgridEditor frame generation", "[analysis][beatgrid]") {
  BeatgridData data;
  data.bpm = 120.0;
  data.firstBeatFrame = 0;
  data.sampleRate = 44100;
  data.downbeatOffset = 0;
  BeatgridEditor editor(data);

  const std::size_t totalAudioFrames = 88200;  // exactly 4 beats
  const auto beats = editor.generateBeatFrames(totalAudioFrames);
  REQUIRE(beats.size() == 4);
  CHECK(beats[0] == 0);
  CHECK(beats[1] == 22050);
  CHECK(beats[2] == 44100);
  CHECK(beats[3] == 66150);

  const auto downbeats = editor.generateDownbeatFrames(totalAudioFrames);
  REQUIRE(downbeats.size() == 1);
  CHECK(downbeats[0] == 0);
}

TEST_CASE("BeatgridData JSON serialization roundtrip", "[analysis][beatgrid]") {
  BeatgridData original;
  original.bpm = 174.5;
  original.firstBeatFrame = 5292;
  original.sampleRate = 44100;
  original.downbeatOffset = 2;
  original.source = "user";

  const std::string json = original.toJson();
  CHECK_FALSE(json.empty());

  BeatgridData restored;
  REQUIRE(BeatgridData::fromJson(json, restored));

  CHECK_THAT(restored.bpm, Catch::Matchers::WithinRel(original.bpm, 1e-4));
  CHECK(restored.firstBeatFrame == original.firstBeatFrame);
  CHECK(restored.sampleRate == original.sampleRate);
  CHECK(restored.downbeatOffset == original.downbeatOffset);
  CHECK(restored.source == "user");
}
