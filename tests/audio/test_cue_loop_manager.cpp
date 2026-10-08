// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <vector>

#include "Audio/Deck/CueLoopManager.hpp"
#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/SyncManager.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "support/AllocationGuard.hpp"

using namespace zyron::audio;

namespace {

std::shared_ptr<TrackBuffer> createTestTrack(int sampleRate, int channels, int numFrames) {
  auto track = std::make_shared<TrackBuffer>(channels, numFrames, static_cast<double>(sampleRate));
  for (int ch = 0; ch < channels; ++ch) {
    float* data = track->channelData(ch);
    if (data) {
      for (int i = 0; i < numFrames; ++i) {
        data[i] = static_cast<float>(i) / static_cast<float>(numFrames);
      }
    }
  }
  return track;
}

}  // namespace

TEST_CASE("CueLoopManager hot cues 8 slots (section 25)", "[audio][cues]") {
  CueLoopManager mgr;
  DeckPlayer player;
  player.prepare(44100.0);
  auto track = createTestTrack(44100, 2, 44100 * 60);
  player.loadTrack(track);

  SECTION("sets and retrieves hot cue with type, color, and name") {
    CHECK_FALSE(mgr.hasHotCue(0));
    CHECK(mgr.setHotCue(0, 44100, "Drop 1", "#FF3366", CueType::Drop));
    CHECK(mgr.hasHotCue(0));

    auto cue0 = mgr.getHotCue(0);
    REQUIRE(cue0.has_value());
    CHECK(cue0->index == 0);
    CHECK(cue0->frame == 44100);
    CHECK(cue0->name == "Drop 1");
    CHECK(cue0->color == "#FF3366");
    CHECK(cue0->type == CueType::Drop);
    CHECK(cue0->active);
  }

  SECTION("supports all cue types: Cue, Loop, Intro, Drop, Break, Outro, Memory") {
    CHECK(mgr.setHotCue(1, 10000, "Intro", "#00FF00", CueType::Intro));
    CHECK(mgr.setHotCue(2, 20000, "Break", "#0000FF", CueType::Break));
    CHECK(mgr.setHotCue(3, 30000, "Loop", "#FFFF00", CueType::Loop));
    CHECK(mgr.setHotCue(4, 40000, "Outro", "#FF00FF", CueType::Outro));
    CHECK(mgr.setHotCue(5, 50000, "Mem", "#FFFFFF", CueType::Memory));
    CHECK(mgr.setHotCue(6, 60000, "Cue", "#00FFFF", CueType::Cue));

    CHECK(mgr.getHotCue(1)->type == CueType::Intro);
    CHECK(mgr.getHotCue(2)->type == CueType::Break);
    CHECK(mgr.getHotCue(3)->type == CueType::Loop);
    CHECK(mgr.getHotCue(4)->type == CueType::Outro);
    CHECK(mgr.getHotCue(5)->type == CueType::Memory);
    CHECK(mgr.getHotCue(6)->type == CueType::Cue);
  }

  SECTION("clears hot cue") {
    mgr.setHotCue(7, 88200, "Last", "#FF8800");
    CHECK(mgr.hasHotCue(7));

    CHECK(mgr.clearHotCue(7));
    CHECK_FALSE(mgr.hasHotCue(7));
    CHECK_FALSE(mgr.getHotCue(7).has_value());
  }

  SECTION("rejects out-of-bounds cue indices") {
    CHECK_FALSE(mgr.setHotCue(-1, 100));
    CHECK_FALSE(mgr.setHotCue(8, 100));
    CHECK_FALSE(mgr.clearHotCue(-1));
    CHECK_FALSE(mgr.clearHotCue(8));
    CHECK_FALSE(mgr.hasHotCue(-1));
    CHECK_FALSE(mgr.hasHotCue(8));
  }

  SECTION("jumpToHotCue seeks player and resumes playback") {
    mgr.setHotCue(0, 15000, "Start");
    player.seek(0);
    player.pause();

    CHECK(mgr.jumpToHotCue(player, 0, true));
    CHECK(player.currentFrame() == 15000);
    CHECK(player.isPlaying());
  }
}

TEST_CASE("CueLoopManager manual and beat loops (section 26)", "[audio][loops]") {
  CueLoopManager mgr;
  DeckPlayer player;
  player.prepare(44100.0);
  auto track = createTestTrack(44100, 2, 44100 * 60);
  player.loadTrack(track);

  DeckGrid grid;
  grid.bpm = 174.0;
  grid.firstBeatFrame = 0;
  grid.sampleRate = 44100;
  // samplesPerBeat = 44100 * 60 / 174 = 15206.89655

  SECTION("manual loop in and loop out") {
    mgr.setLoopIn(player, 10000);
    CHECK_FALSE(mgr.isLoopActive());

    mgr.setLoopOut(player, 25207);
    CHECK(mgr.isLoopActive());
    CHECK(mgr.loopStartFrame() == 10000);
    CHECK(mgr.loopEndFrame() == 25207);
    CHECK(player.isLoopActive());

    mgr.exitLoop(player);
    CHECK_FALSE(mgr.isLoopActive());
    CHECK_FALSE(player.isLoopActive());

    mgr.reloop(player);
    CHECK(mgr.isLoopActive());
    CHECK(player.isLoopActive());
  }

  SECTION("quantized beat loop 4 beats, halve, double, move") {
    player.seek(0);
    mgr.setBeatLoop(player, 4.0, grid);

    CHECK(mgr.isLoopActive());
    CHECK(mgr.currentLoopBeats() == 4.0);
    const auto expectedLen = static_cast<std::int64_t>(std::round(4.0 * grid.samplesPerBeat()));
    CHECK(mgr.loopStartFrame() == 0);
    CHECK(mgr.loopEndFrame() == expectedLen);

    // Halve loop: 4 beats -> 2 beats
    mgr.halveLoop(player, grid);
    CHECK(mgr.currentLoopBeats() == 2.0);
    const auto halveLen = static_cast<std::int64_t>(std::round(2.0 * grid.samplesPerBeat()));
    CHECK(mgr.loopEndFrame() == halveLen);

    // Double loop: 2 beats -> 4 beats
    mgr.doubleLoop(player, grid);
    CHECK(mgr.currentLoopBeats() == 4.0);
    CHECK(mgr.loopEndFrame() == expectedLen);

    // Move loop forward by 8 beats
    mgr.moveLoop(player, 8.0, grid);
    const auto shift = static_cast<std::int64_t>(std::round(8.0 * grid.samplesPerBeat()));
    CHECK(mgr.loopStartFrame() == shift);
    CHECK(mgr.loopEndFrame() == shift + expectedLen);
  }

  SECTION("DeckPlayer render loops seamlessly without interruption") {
    player.seek(1000);
    player.setLoop(1000, 2000);
    player.setLoopActive(true);
    player.play();

    std::vector<float> left(1500, 0.0F);
    std::vector<float> right(1500, 0.0F);
    float* channels[2] = {left.data(), right.data()};

    // Render 1500 samples (from 1000 up to 2000, wrapping back to 1000 + 500 = 1500)
    player.render(channels, 2, 1500);

    CHECK(player.isPlaying());
    // Playhead wrapped around inside [1000, 2000)
    CHECK(player.currentFrame() >= 1000);
    CHECK(player.currentFrame() < 2000);
  }

  SECTION("realtime safety guard during loop operations") {
    zyron::test::ScopedRealtimeGuard guard;
    mgr.setBeatLoop(player, 2.0, grid);
    mgr.halveLoop(player, grid);
    mgr.doubleLoop(player, grid);
    mgr.exitLoop(player);
    mgr.reloop(player);

    CHECK_FALSE(guard.report().hasViolations());
  }
}
