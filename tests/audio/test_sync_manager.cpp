// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <memory>
#include <vector>

#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/SyncManager.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "support/AllocationGuard.hpp"

using namespace zyron::audio;
using namespace zyron::core;

namespace {

std::shared_ptr<TrackBuffer> createTestTrack(int sampleRate, int channels, int numFrames) {
  auto track = std::make_shared<TrackBuffer>(channels, numFrames, static_cast<double>(sampleRate));
  for (int ch = 0; ch < channels; ++ch) {
    float* data = track->channelData(ch);
    if (data) {
      std::fill_n(data, numFrames, 0.5f);
    }
  }
  return track;
}

}  // namespace

TEST_CASE("SyncManager master deck selection and auto-master", "[audio][sync]") {
  SyncManager sync;

  DeckPlayer playerA;
  DeckPlayer playerB;
  playerA.prepare(44100.0);
  playerB.prepare(44100.0);

  auto track = createTestTrack(44100, 2, 44100 * 10);
  playerA.loadTrack(track);
  playerB.loadTrack(track);

  std::array<DeckPlayer*, kDeckCount> players = {&playerA, &playerB, nullptr, nullptr};

  SECTION("initial state has no master deck") {
    CHECK_FALSE(sync.masterDeck().has_value());
  }

  SECTION("manual master assignment") {
    sync.setMasterDeck(DeckId::A);
    REQUIRE(sync.masterDeck().has_value());
    CHECK(sync.masterDeck().value() == DeckId::A);

    sync.setMasterDeck(DeckId::B);
    REQUIRE(sync.masterDeck().has_value());
    CHECK(sync.masterDeck().value() == DeckId::B);
  }

  SECTION("auto-master elects first playing deck and fails over on stop") {
    sync.setAutoMasterEnabled(true);

    // No decks playing yet -> no master elected
    sync.update(players);
    CHECK_FALSE(sync.masterDeck().has_value());

    // Start player A -> Deck A becomes master
    playerA.play();
    sync.update(players);
    REQUIRE(sync.masterDeck().has_value());
    CHECK(sync.masterDeck().value() == DeckId::A);

    // Start player B while A is still playing -> Deck A remains master
    playerB.play();
    sync.update(players);
    REQUIRE(sync.masterDeck().has_value());
    CHECK(sync.masterDeck().value() == DeckId::A);

    // Stop player A -> Deck B automatically assumes master role!
    playerA.pause();
    sync.update(players);
    REQUIRE(sync.masterDeck().has_value());
    CHECK(sync.masterDeck().value() == DeckId::B);
  }
}

TEST_CASE("SyncManager tempo matching and octave resolution", "[audio][sync]") {
  SyncManager sync;

  DeckGrid gridDnB;
  gridDnB.bpm = 174.0;
  gridDnB.sampleRate = 44100;

  DeckGrid gridHouse;
  gridHouse.bpm = 128.0;
  gridHouse.sampleRate = 44100;

  DeckGrid gridHalfDnB;
  gridHalfDnB.bpm = 87.0;  // 174 / 2
  gridHalfDnB.sampleRate = 44100;

  sync.setDeckGrid(DeckId::A, gridDnB);
  sync.setDeckGrid(DeckId::B, gridDnB);
  sync.setDeckGrid(DeckId::C, gridHouse);
  sync.setDeckGrid(DeckId::D, gridHalfDnB);

  SECTION("matching identical BPM yields speed 1.0") {
    const double speed = sync.calculateTempoMatchSpeed(DeckId::B, DeckId::A, 1.0);
    CHECK_THAT(speed, Catch::Matchers::WithinRel(1.0, 1e-4));
  }

  SECTION("matching tracks tracks master pitch bends") {
    // Master at +4% (speed = 1.04) -> target also speeds up to 1.04
    const double speed = sync.calculateTempoMatchSpeed(DeckId::B, DeckId::A, 1.04);
    CHECK_THAT(speed, Catch::Matchers::WithinRel(1.04, 1e-4));
  }

  SECTION("matching 128 BPM to 174 BPM with pitch shift") {
    // 174 / 128 = 1.359375
    const double speed = sync.calculateTempoMatchSpeed(DeckId::C, DeckId::A, 1.0);
    CHECK_THAT(speed, Catch::Matchers::WithinRel(174.0 / 128.0, 1e-4));
  }

  SECTION("octave-aware resolution for DnB half-time (87 vs 174 BPM)") {
    // A is 174 BPM, D is 87 BPM. Rather than speeding D up by +100% (2.0x),
    // octave resolution maintains 1.0x (half-time locked)!
    const double speedD = sync.calculateTempoMatchSpeed(DeckId::D, DeckId::A, 1.0);
    CHECK_THAT(speedD, Catch::Matchers::WithinRel(1.0, 1e-4));

    // And vice-versa: Master is 87 BPM, Target is 174 BPM -> stays 1.0x
    const double speedA = sync.calculateTempoMatchSpeed(DeckId::A, DeckId::D, 1.0);
    CHECK_THAT(speedA, Catch::Matchers::WithinRel(1.0, 1e-4));
  }
}

TEST_CASE("SyncManager sample-accurate phase alignment", "[audio][sync]") {
  SyncManager sync;

  DeckPlayer playerA;
  DeckPlayer playerB;
  playerA.prepare(44100.0);
  playerB.prepare(44100.0);

  auto track = createTestTrack(44100, 2, 44100 * 30);
  playerA.loadTrack(track);
  playerB.loadTrack(track);

  DeckGrid grid;
  grid.bpm = 174.0;
  grid.firstBeatFrame = 0;
  grid.sampleRate = 44100;
  // Samples per beat = 44100 * 60 / 174 = 15206.89655

  sync.setDeckGrid(DeckId::A, grid);
  sync.setDeckGrid(DeckId::B, grid);

  SECTION("aligns phase when target is behind master") {
    // Player A is at 25% through beat (frame ~ 3802)
    playerA.seek(3802);
    // Player B is at 0% through beat (frame 0)
    playerB.seek(0);

    const auto shift = sync.alignPhase(playerB, DeckId::B, playerA, DeckId::A);
    CHECK(shift > 0);

    const double phaseA = grid.beatFraction(playerA.currentFrame());
    const double phaseB = grid.beatFraction(playerB.currentFrame());
    CHECK_THAT(phaseB, Catch::Matchers::WithinAbs(phaseA, 1e-4));
  }

  SECTION("aligns phase when target is ahead of master (within nearest half beat)") {
    // Player A is at 10% through beat
    playerA.seek(1521);
    // Player B is at 30% through beat
    playerB.seek(4562);

    const auto shift = sync.alignPhase(playerB, DeckId::B, playerA, DeckId::A);
    CHECK(shift < 0);

    const double phaseA = grid.beatFraction(playerA.currentFrame());
    const double phaseB = grid.beatFraction(playerB.currentFrame());
    CHECK_THAT(phaseB, Catch::Matchers::WithinAbs(phaseA, 1e-4));
  }
}

TEST_CASE("SyncManager dynamic tracking and realtime safety", "[audio][sync]") {
  SyncManager sync;

  DeckPlayer playerA;
  DeckPlayer playerB;
  playerA.prepare(44100.0);
  playerB.prepare(44100.0);

  auto track = createTestTrack(44100, 2, 44100 * 30);
  playerA.loadTrack(track);
  playerB.loadTrack(track);

  DeckGrid grid;
  grid.bpm = 174.0;
  grid.firstBeatFrame = 0;
  grid.sampleRate = 44100;

  sync.setDeckGrid(DeckId::A, grid);
  sync.setDeckGrid(DeckId::B, grid);

  sync.setMasterDeck(DeckId::A);
  sync.setSyncEnabled(DeckId::B, true);

  std::array<DeckPlayer*, kDeckCount> players = {&playerA, &playerB, nullptr, nullptr};

  SECTION("update dynamically adjusts synced deck speed to master pitch") {
    playerA.setPlaybackSpeed(1.06);  // +6% pitch bend on master
    sync.update(players);

    CHECK_THAT(playerB.playbackSpeed(), Catch::Matchers::WithinRel(1.06, 1e-4));
  }

  SECTION("full syncDeck and update are strictly realtime allocation-free") {
    playerA.seek(4000);
    playerB.seek(1000);

    zyron::test::ScopedRealtimeGuard guard;
    sync.syncDeck(playerB, DeckId::B, playerA, DeckId::A);
    sync.update(players);

    CHECK_FALSE(guard.report().hasViolations());
  }
}
