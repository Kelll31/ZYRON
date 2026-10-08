// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "Analysis/Bpm/Beatgrid.hpp"
#include "Analysis/Structure/StructureSegmenter.hpp"
#include "Analysis/Structure/StructureTypes.hpp"
#include "Library/Database/Database.hpp"
#include "Library/Database/LibraryRepository.hpp"
#include "Library/Database/Migrations.hpp"

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Synthesizes a structured EDM test track (Intro -> Build -> Drop 1 -> Break -> Drop 2 -> Outro)
std::vector<float> generateStructuredTrack(int sampleRate, int totalSeconds) {
  const std::size_t totalSamples = static_cast<std::size_t>(totalSeconds * sampleRate);
  std::vector<float> audio(totalSamples, 0.0f);

  for (std::size_t i = 0; i < totalSamples; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
    float sample = 0.0f;

    if (t < 20.0) {
      // Intro: gentle rhythmic tick and mild 220 Hz pad
      const float pad = 0.15f * std::sin(2.0f * kPi * 220.0f * static_cast<float>(t));
      const float tick = (std::fmod(t, 0.5) < 0.02) ? 0.25f : 0.0f;
      sample = pad + tick;
    } else if (t < 35.0) {
      // Build: rising volume, rising frequency sweep, snare roll
      const float ramp = static_cast<float>((t - 20.0) / 15.0);
      const float sweepFreq = 200.0f + 1200.0f * ramp;
      const float sweep = 0.35f * ramp * std::sin(2.0f * kPi * sweepFreq * static_cast<float>(t));
      const float snare = (std::fmod(t, 0.25) < 0.03) ? (0.4f * ramp) : 0.0f;
      sample = sweep + snare;
    } else if (t < 65.0) {
      // Drop 1: heavy 50 Hz sub-bass + loud 120 BPM kick drum
      const float sub = 0.60f * std::sin(2.0f * kPi * 50.0f * static_cast<float>(t));
      const double kickPhase = std::fmod(t, 0.5);  // 120 BPM = 0.5s per beat
      const float kick = (kickPhase < 0.15) ? (0.80f * std::exp(static_cast<float>(-kickPhase * 25.0))) : 0.0f;
      sample = 0.6f * sub + 0.4f * kick;
    } else if (t < 85.0) {
      // Break: quiet atmospheric pad, no sub-bass or heavy kick
      const float pad = 0.12f * std::sin(2.0f * kPi * 330.0f * static_cast<float>(t));
      sample = pad;
    } else if (t < 105.0) {
      // Drop 2: maximum energy, heavy sub + kicks
      const float sub = 0.70f * std::sin(2.0f * kPi * 55.0f * static_cast<float>(t));
      const double kickPhase = std::fmod(t, 0.5);
      const float kick = (kickPhase < 0.15) ? (0.85f * std::exp(static_cast<float>(-kickPhase * 25.0))) : 0.0f;
      sample = 0.6f * sub + 0.4f * kick;
    } else {
      // Outro: stripped-down beat, fading out
      const float fade = static_cast<float>((120.0 - t) / 15.0);
      const float tick = (std::fmod(t, 0.5) < 0.02) ? (0.25f * fade) : 0.0f;
      sample = tick;
    }

    audio[i] = sample;
  }

  return audio;
}

}  // namespace

TEST_CASE("StructureSegmenter: short audio fallback", "[analysis][structure]") {
  zyron::analysis::StructureSegmenter segmenter;
  const int sampleRate = 44100;
  std::vector<float> shortAudio(sampleRate * 3, 0.1f);  // 3 seconds

  const auto structure = segmenter.analyze(shortAudio.data(), shortAudio.size(), sampleRate);
  REQUIRE_FALSE(structure.empty());
  REQUIRE(structure.segments.size() == 1);
  CHECK(structure.segments[0].type == zyron::analysis::SegmentType::Intro);
  CHECK(structure.segments[0].startFrame == 0);
  CHECK(structure.segments[0].endFrame == static_cast<std::int64_t>(shortAudio.size()));
}

TEST_CASE("StructureSegmenter: structured track segmentation and bar alignment", "[analysis][structure]") {
  const int sampleRate = 44100;
  const int durationSec = 120;
  const auto audio = generateStructuredTrack(sampleRate, durationSec);

  zyron::analysis::BeatgridData grid;
  grid.bpm = 120.0;
  grid.sampleRate = sampleRate;
  grid.firstBeatFrame = 0;

  zyron::analysis::StructureSegmenter segmenter;
  const auto structure = segmenter.analyze(audio.data(), audio.size(), sampleRate, &grid);

  REQUIRE_FALSE(structure.empty());
  CHECK(structure.durationSec >= 119.0);
  CHECK(structure.totalFrames == static_cast<std::int64_t>(audio.size()));

  // Verify contiguous boundary coverage
  CHECK(structure.segments.front().startFrame == 0);
  CHECK(structure.segments.back().endFrame == structure.totalFrames);
  for (std::size_t i = 1; i < structure.segments.size(); ++i) {
    CHECK(structure.segments[i].startFrame == structure.segments[i - 1].endFrame);
    CHECK(structure.segments[i].startSec >= structure.segments[i - 1].startSec);
  }

  // Verify key structural sections were detected
  const auto intro = structure.intro();
  const auto firstDrop = structure.firstDrop();
  const auto breakdown = structure.breakdown();
  const auto outro = structure.outro();

  REQUIRE(intro.has_value());
  REQUIRE(firstDrop.has_value());
  REQUIRE(breakdown.has_value());
  REQUIRE(outro.has_value());

  CHECK(intro->startSec < 5.0);
  CHECK(firstDrop->startSec >= 25.0);
  CHECK(firstDrop->startSec <= 45.0);
  CHECK(breakdown->startSec >= 60.0);
  CHECK(breakdown->startSec <= 85.0);
  CHECK(outro->startSec >= 95.0);

  // Drop 1 energy must be significantly higher than Intro and Break
  CHECK(firstDrop->averageEnergy > intro->averageEnergy);
  CHECK(firstDrop->averageEnergy > breakdown->averageEnergy);
  CHECK(firstDrop->peakEnergy >= 5.0f);

  // Check bar quantization
  CHECK(intro->startBar == 1);
  CHECK(firstDrop->startBar > 1);
  CHECK(breakdown->startBar > firstDrop->startBar);

  // Check findSegmentAtTime
  const auto segAt50 = structure.findSegmentAtTime(50.0);
  REQUIRE(segAt50.has_value());
  CHECK(segAt50->type == zyron::analysis::SegmentType::Drop);
}

TEST_CASE("StructureSegmenter: MixPointCue generation and database persistence (P6-03)", "[analysis][structure]") {
  const int sampleRate = 44100;
  const int durationSec = 120;
  const auto audio = generateStructuredTrack(sampleRate, durationSec);

  zyron::analysis::BeatgridData grid;
  grid.bpm = 120.0;
  grid.sampleRate = sampleRate;
  grid.firstBeatFrame = 0;

  zyron::analysis::StructureSegmenter segmenter;
  const auto structure = segmenter.analyze(audio.data(), audio.size(), sampleRate, &grid);

  const auto cues = segmenter.generateMixPointCues(structure);
  REQUIRE_FALSE(cues.empty());

  // Check cues contain Intro, Drop 1, Breakdown, Outro
  bool foundIntro = false;
  bool foundDrop = false;
  bool foundBreak = false;
  bool foundOutro = false;

  for (const auto& c : cues) {
    CHECK(c.frame >= 0);
    CHECK(c.timeSec >= 0.0);
    CHECK(!c.color.empty());
    CHECK(!c.name.empty());
    if (c.cueType == "intro") foundIntro = true;
    if (c.cueType == "drop") foundDrop = true;
    if (c.cueType == "break") foundBreak = true;
    if (c.cueType == "outro") foundOutro = true;
  }

  CHECK(foundIntro);
  CHECK(foundDrop);
  CHECK(foundBreak);
  CHECK(foundOutro);

  // Verify persistence into SQLite database and user cue protection
  auto db = zyron::library::Database::openInMemory();
  zyron::library::Migrations::apply(db);

  zyron::library::TrackRecord track;
  track.filepath = "C:/Music/test_structure.wav";
  track.contentHash = "test_hash_123";
  const std::int64_t trackId = zyron::library::LibraryRepository::insertTrack(db, track);
  REQUIRE(trackId > 0);

  // 1. Save auto cues
  for (const auto& c : cues) {
    zyron::library::CuePointRecord record;
    record.trackId = trackId;
    record.index = c.hotCueSlot;
    record.frame = c.frame;
    record.name = c.name;
    record.color = c.color;
    record.type = c.cueType;
    record.source = "auto";
    zyron::library::LibraryRepository::saveCuePoint(db, record);
  }

  auto loadedCues = zyron::library::LibraryRepository::getCuePoints(db, trackId);
  CHECK(loadedCues.size() == cues.size());

  // 2. User edits slot 1 to custom cue with source="user"
  zyron::library::CuePointRecord userCue;
  userCue.trackId = trackId;
  userCue.index = 1;
  userCue.frame = 123456;
  userCue.name = "My Custom User Cue";
  userCue.color = "#FFFFFF";
  userCue.type = "drop";
  userCue.source = "user";
  zyron::library::LibraryRepository::saveCuePoint(db, userCue);

  // 3. Re-run analysis and save auto cues again
  for (const auto& c : cues) {
    zyron::library::CuePointRecord record;
    record.trackId = trackId;
    record.index = c.hotCueSlot;
    record.frame = c.frame;
    record.name = c.name;
    record.color = c.color;
    record.type = c.cueType;
    record.source = "auto";
    zyron::library::LibraryRepository::saveCuePoint(db, record);
  }

  // 4. Verify user cue at slot 1 was NOT overwritten by auto analysis!
  loadedCues = zyron::library::LibraryRepository::getCuePoints(db, trackId);
  bool userCuePreserved = false;
  for (const auto& c : loadedCues) {
    if (c.index == 1) {
      CHECK(c.source == "user");
      CHECK(c.name == "My Custom User Cue");
      CHECK(c.frame == 123456);
      userCuePreserved = true;
    }
  }
  CHECK(userCuePreserved);
}

#include "Analysis/Structure/MixPointFinder.hpp"

namespace {

/// 174 BPM: 16 bars intro (hats only), 32 bars drop (hats + 50 Hz bass), 16 bars outro (hats), then a silent minute.
std::vector<float> dnbWithSilentTail(int sampleRate) {
  const double bar = 4.0 * 60.0 / 174.0;
  const double introEnd = 16 * bar;
  const double dropEnd = introEnd + 32 * bar;
  const double outroEnd = dropEnd + 16 * bar;
  const auto frames = static_cast<std::size_t>((outroEnd + 60.0) * sampleRate);
  std::vector<float> mono(frames, 0.0F);
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    if (t >= outroEnd) {
      break;
    }
    float v = 0.15F * static_cast<float>(std::sin(2.0 * 3.14159265 * 6000.0 * t));
    if (t >= introEnd && t < dropEnd) {
      v += 0.6F * static_cast<float>(std::sin(2.0 * 3.14159265 * 50.0 * t));
    }
    mono[i] = v;
  }
  return mono;
}

}  // namespace

TEST_CASE("MixPointFinder: drop, mix out before the outro, silent tail never played", "[analysis][mixpoints]") {
  constexpr int kRate = 22050;
  const auto mono = dnbWithSilentTail(kRate);
  const double bar = 4.0 * 60.0 / 174.0;

  zyron::analysis::MixPointInput input;
  input.mono = mono.data();
  input.frames = mono.size();
  input.sampleRate = kRate;
  input.bpm = 174.0;
  input.firstBeatSec = 0.0;
  input.transitionBeats = 32.0;
  const auto points = zyron::analysis::findMixPoints(input);

  CHECK(points.audibleEndSec < 64 * bar + 1.0);  // the silent minute is not part of the music
  CHECK(points.mixInSec < 0.5);
  REQUIRE(points.dropSec > 0.0);
  CHECK(std::abs(points.dropSec - 16 * bar) < bar);
  // The outro starts at bar 48: mix out there, and a whole 32-beat transition still fits before the sound ends.
  CHECK(std::abs(points.mixOutSec - 48 * bar) < bar + 0.01);
  CHECK(points.mixOutSec + 32 * 60.0 / 174.0 <= points.audibleEndSec + 1e-6);
}

TEST_CASE("MixPointFinder: silence only finds nothing to mix", "[analysis][mixpoints]") {
  std::vector<float> silence(22050 * 10, 0.0F);
  zyron::analysis::MixPointInput input;
  input.mono = silence.data();
  input.frames = silence.size();
  input.sampleRate = 22050;
  input.bpm = 174.0;
  const auto points = zyron::analysis::findMixPoints(input);
  CHECK(points.dropSec < 0.0);
  CHECK(points.mixOutSec < 0.0);
}

TEST_CASE("MixPointFinder: a micro drop at the end is never reached by the mix out", "[analysis][mixpoints]") {
  constexpr int kRate = 22050;
  const double bar = 4.0 * 60.0 / 174.0;
  // intro 16 bars, drop 32 bars, outro 16 bars, micro drop 4 bars, tail 8 bars.
  const double dropStart = 16 * bar;
  const double dropEnd = 48 * bar;
  const double microStart = 64 * bar;
  const double microEnd = 68 * bar;
  const double end = 76 * bar;
  std::vector<float> mono(static_cast<std::size_t>(end * kRate), 0.0F);
  for (std::size_t i = 0; i < mono.size(); ++i) {
    const double t = static_cast<double>(i) / kRate;
    float v = 0.15F * static_cast<float>(std::sin(2.0 * 3.14159265 * 6000.0 * t));
    if ((t >= dropStart && t < dropEnd) || (t >= microStart && t < microEnd)) {
      v += 0.6F * static_cast<float>(std::sin(2.0 * 3.14159265 * 50.0 * t));
    }
    mono[i] = v;
  }
  zyron::analysis::MixPointInput input;
  input.mono = mono.data();
  input.frames = mono.size();
  input.sampleRate = kRate;
  input.bpm = 174.0;
  input.transitionBeats = 32.0;
  const auto points = zyron::analysis::findMixPoints(input);

  REQUIRE(points.drops.size() >= 2);  // the micro drop is known
  CHECK(std::abs(points.drops.back() - microStart) < bar);
  CHECK(points.mixOutSec + 32 * 60.0 / 174.0 <= microStart + 1e-6);  // the whole transition ends before it
  CHECK(points.mixOutSec > dropStart);
}
