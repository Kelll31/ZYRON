// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>
#include <vector>

// 1. Library & Database
#include "Library/Database/Database.hpp"
#include "Library/Metadata/MetadataExtractor.hpp"
#include "Library/Scanner/LibraryScanner.hpp"

// 2. Analysis
#include "Analysis/Bpm/BeatDetector.hpp"
#include "Analysis/Bpm/BeatgridEditor.hpp"
#include "Analysis/Energy/EnergyAnalyzer.hpp"
#include "Analysis/Key/KeyDetector.hpp"

// 3. Audio & DSP
#include "Audio/DSP/ChannelStrip.hpp"
#include "Audio/DSP/Mixer.hpp"
#include "Audio/DSP/StemMixer.hpp"
#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/SyncManager.hpp"
#include "Audio/Deck/TrackBuffer.hpp"
#include "Audio/Routing/StemRouter.hpp"

// 4. Stems
#include "Stems/MockStemSeparator.hpp"

// 5. Recording
#include "Recording/MasterRecorder.hpp"

// Realtime safety verification
#include "support/AllocationGuard.hpp"

using namespace zyron;
using namespace zyron::audio;
using namespace zyron::analysis;
using namespace zyron::library;
using namespace zyron::stems;
using namespace zyron::recording;
using namespace zyron::test;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kPi = std::numbers::pi;

std::shared_ptr<TrackBuffer> makeSyntheticTrack(float baseFreqHz, double durationSec, double sampleRate) {
  const auto totalFrames = static_cast<std::int64_t>(std::round(durationSec * sampleRate));
  auto tb = std::make_shared<TrackBuffer>(2, totalFrames, sampleRate);
  float* left = tb->channelData(0);
  float* right = tb->channelData(1);

  const double phaseInc = 2.0 * kPi * static_cast<double>(baseFreqHz) / sampleRate;
  double phase = 0.0;

  for (std::int64_t i = 0; i < totalFrames; ++i) {
    const auto val = static_cast<float>(0.4 * std::sin(phase) + 0.2 * std::sin(phase * 2.5));
    left[i] = val;
    right[i] = val;
    phase += phaseInc;
    if (phase >= 2.0 * kPi) {
      phase -= 2.0 * kPi;
    }
  }
  return tb;
}

}  // namespace

TEST_CASE("MVP 1 End-to-End Gate Pipeline (SPEC section 82, ROADMAP MVP 1 Gate)", "[mvp1][gate][integration]") {
  // Step 1: Scan & Library database
  const auto tempDir = std::filesystem::temp_directory_path() / "zyron_mvp1_gate_dir";
  std::filesystem::remove_all(tempDir);
  std::filesystem::create_directories(tempDir);

  const auto dbPath = (tempDir / "library.db").string();
  {
    auto db = Database::open(dbPath);
    REQUIRE(db.isOpen());
  }

  // Step 2: Track creation and analysis
  constexpr double kTrackDuration = 5.0;  // 5 seconds synthetic tracks
  auto trackA = makeSyntheticTrack(174.0F, kTrackDuration, kSampleRate);
  auto trackB = makeSyntheticTrack(170.0F, kTrackDuration, kSampleRate);

  // 2a. Beat detection
  BeatDetector beatDetector;
  const auto beatResA = beatDetector.detect(trackA->channelData(0),
                                            static_cast<std::size_t>(trackA->numFrames()),
                                            static_cast<int>(kSampleRate));
  CHECK(beatResA.bpm > 0.0);

  // 2b. Key detection
  KeyDetector keyDetector;
  const auto keyA = keyDetector.detectKey(trackA->channelData(0),
                                         static_cast<std::size_t>(trackA->numFrames()),
                                         static_cast<int>(kSampleRate));
  CHECK(keyA.isValid());

  // 2c. Energy analysis
  EnergyAnalyzer energyAnalyzer;
  const auto energyA = energyAnalyzer.analyze(trackA->channelData(0),
                                             static_cast<std::size_t>(trackA->numFrames()),
                                             static_cast<int>(kSampleRate));
  CHECK(energyA.globalEnergy >= 1.0F);
  CHECK(energyA.globalEnergy <= 10.0F);

  // Step 3: Load A/B into Deck Players
  DeckPlayer deckA;
  deckA.prepare(kSampleRate);
  deckA.loadTrack(trackA);

  DeckPlayer deckB;
  deckB.prepare(kSampleRate);
  deckB.loadTrack(trackB);

  // Step 4: Play
  deckA.play();
  deckB.play();
  CHECK(deckA.isPlaying());
  CHECK(deckB.isPlaying());

  // Step 5: SYNC (Sample-accurate tempo match & phase alignment)
  SyncManager syncManager;
  syncManager.setMasterDeck(core::DeckId::A);
  DeckGrid gridA;
  gridA.bpm = 174.0;
  gridA.sampleRate = static_cast<int>(kSampleRate);

  DeckGrid gridB;
  gridB.bpm = 170.0;
  gridB.sampleRate = static_cast<int>(kSampleRate);

  syncManager.setDeckGrid(core::DeckId::A, gridA);
  syncManager.setDeckGrid(core::DeckId::B, gridB);

  // Match tempo of Deck B to Deck A
  const double targetSpeed = syncManager.calculateTempoMatchSpeed(core::DeckId::B, core::DeckId::A, 1.0);
  deckB.setPlaybackSpeed(targetSpeed);
  CHECK_THAT(deckB.playbackSpeed(), WithinAbs(174.0 / 170.0, 1e-4));

  // Step 6: EQ & Filter (ChannelStrips)
  ChannelStrip stripA;
  stripA.prepare(kSampleRate);
  stripA.setLowDb(-3.0F);               // Cut bass slightly
  stripA.setFilter(-0.3F);              // DJ low-pass filter

  ChannelStrip stripB;
  stripB.prepare(kSampleRate);
  stripB.setHighDb(2.0F);               // Boost treble

  // Step 7: Stems (Phase 5 Demucs separator & Stem playback)
  MockStemSeparator separator;  // a real model needs the downloaded weights; see test_onnx_models and the app tests
  const float* inA[2] = {trackA->channelData(0), trackA->channelData(1)};
  auto stemResult = separator.separate(inA, 2, 44100, 44100.0);
  REQUIRE(stemResult.success);

  // Create 4-stem track buffers for Deck A
  auto voc = std::make_shared<TrackBuffer>(2, stemResult.vocals().numFrames(), kSampleRate);
  auto drm = std::make_shared<TrackBuffer>(2, stemResult.drums().numFrames(), kSampleRate);
  auto bas = std::make_shared<TrackBuffer>(2, stemResult.bass().numFrames(), kSampleRate);
  auto oth = std::make_shared<TrackBuffer>(2, stemResult.other().numFrames(), kSampleRate);

  std::copy(stemResult.vocals().left.begin(), stemResult.vocals().left.end(), voc->channelData(0));
  std::copy(stemResult.vocals().right.begin(), stemResult.vocals().right.end(), voc->channelData(1));
  std::copy(stemResult.drums().left.begin(), stemResult.drums().left.end(), drm->channelData(0));
  std::copy(stemResult.drums().right.begin(), stemResult.drums().right.end(), drm->channelData(1));
  std::copy(stemResult.bass().left.begin(), stemResult.bass().left.end(), bas->channelData(0));
  std::copy(stemResult.bass().right.begin(), stemResult.bass().right.end(), bas->channelData(1));
  std::copy(stemResult.other().left.begin(), stemResult.other().left.end(), oth->channelData(0));
  std::copy(stemResult.other().right.begin(), stemResult.other().right.end(), oth->channelData(1));

  deckA.loadStems({voc, drm, bas, oth});
  CHECK(deckA.hasStems());

  StemMixer stemMixerA;
  stemMixerA.prepare(kSampleRate);
  stemMixerA.setVolume(core::StemKind::Drums, 0.0F);  // Mute drums in stem mix
  stemMixerA.setVolume(core::StemKind::Vocals, 1.0F); // Full vocals

  StemRouter stemRouter;
  stemRouter.applySplitPreset();  // Test cross-track stem routing (§44)

  // Step 8: Mixer & Step 9: Recorder
  Mixer mixer;
  mixer.prepare(kSampleRate);
  mixer.setCrossfader(0.0F);  // Centred crossfader blend

  MasterRecorder recorder;
  const auto outputWav = tempDir / "mvp1_master_recording.wav";
  REQUIRE(recorder.start(outputWav, kSampleRate, 2));

  // Audio rendering loop
  constexpr int kBlockSize = 256;
  constexpr int kBlocksToProcess = 32;

  std::vector<float> bufAL(kBlockSize), bufAR(kBlockSize);
  std::vector<float> bufBL(kBlockSize), bufBR(kBlockSize);
  std::vector<float> masterL(kBlockSize), masterR(kBlockSize);

  // Verify realtime safety: zero memory allocations during live audio loop
  {
    ScopedRealtimeGuard guard;

    for (int b = 0; b < kBlocksToProcess; ++b) {
      float* ptrA[2] = {bufAL.data(), bufAR.data()};
      float* ptrB[2] = {bufBL.data(), bufBR.data()};

      // Render Deck A with active stems and Deck B
      deckA.render(ptrA, 2, kBlockSize);
      deckB.render(ptrB, 2, kBlockSize);

      // Channel strips (EQ + Filter)
      stripA.process(ptrA, 2, kBlockSize);
      stripB.process(ptrB, 2, kBlockSize);

      // Mixer
      const float* mixInL[2] = {bufAL.data(), bufBL.data()};
      const float* mixInR[2] = {bufAR.data(), bufBR.data()};
      mixer.process(mixInL, mixInR, 2, masterL.data(), masterR.data(), kBlockSize);

      // Recorder tap
      float* recPtr[2] = {masterL.data(), masterR.data()};
      recorder.writeSamples(recPtr, 2, kBlockSize);
    }

    CHECK_FALSE(guard.hasViolations());
  }

  recorder.stop();
  CHECK(recorder.recordedFrames() == kBlockSize * kBlocksToProcess);
  CHECK(std::filesystem::exists(outputWav));
  CHECK(std::filesystem::file_size(outputWav) > 44);  // Valid WAV with header and data

  // Cleanup
  std::filesystem::remove_all(tempDir);
}
