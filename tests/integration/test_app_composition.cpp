// SPDX-License-Identifier: AGPL-3.0-only
//
// App-level test (ROADMAP P11): builds the SAME composition Main.cpp runs - library, command bus, audio engine - and
// drives it the way the UI does: scan a folder, load two tracks, play, mix. The device callback is called directly,
// so no sound card is needed; everything else is the real code path.
#include <juce_events/juce_events.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include "Application/AnalysisSidecar.hpp"
#include "Application/ApplicationComposition.hpp"
#include "Application/TrackAnalyzer.hpp"
#include "Audio/Decoder/WavDecoder.hpp"
#include "Library/Database/LibraryRepository.hpp"

using namespace zyron;
using namespace std::chrono_literals;
using Catch::Matchers::WithinAbs;

namespace {

constexpr int kBlock = 256;
const core::CommandOrigin kOrigin{core::CommandOrigin::Kind::Script, "app-composition-test"};

void writeSineWav(const std::filesystem::path& path, double frequencyHz, double sampleRate, double seconds) {
  const auto frames = static_cast<std::int64_t>(seconds * sampleRate);
  audio::TrackBuffer buffer(2, frames, sampleRate);
  for (std::int64_t i = 0; i < frames; ++i) {
    const double phase = 2.0 * std::numbers::pi * frequencyHz * static_cast<double>(i) / sampleRate;
    const auto value = static_cast<float>(0.5 * std::sin(phase));
    buffer.channelData(0)[i] = value;
    buffer.channelData(1)[i] = value;
  }
  REQUIRE(audio::WavDecoder::encode(path, buffer, true));
}

template <typename Predicate>
bool waitFor(Predicate&& predicate, std::chrono::milliseconds timeout = 15s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(20ms);
  }
  return predicate();
}

/// Plays `blocks` device callbacks and returns the RMS of everything that came out of the left channel.
double renderRms(audio::AudioEngine& engine, int blocks) {
  std::vector<float> left(kBlock);
  std::vector<float> right(kBlock);
  double sum = 0.0;
  for (int b = 0; b < blocks; ++b) {
    float* channels[2] = {left.data(), right.data()};
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, channels, 2, kBlock, {});
    for (const float sample : left) {
      sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
  }
  return std::sqrt(sum / static_cast<double>(blocks * kBlock));
}

void submit(application::ApplicationComposition& app, const core::Command& command) {
  const auto error = app.bus().submit(command, kOrigin);
  INFO(std::string(core::commandName(command)) << ": " << (error ? error->message : std::string{}));
  REQUIRE_FALSE(error.has_value());
}

std::filesystem::path toPath(const std::string& utf8) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

/// Four 60 s tracks (two more than the fixture) with known tempo, grid and energy, ready for Automix. 60 s keeps the
/// run short: the loop prepares the next track at 50 s left and starts mixing at 30 s left.
void prepareSyntheticSet(application::ApplicationComposition& app, const std::filesystem::path& music,
                         const std::filesystem::path& root) {
  // The fixture's own two tones are only 4 s long (the set builder skips tracks under 30 s): make all four long.
  writeSineWav(music / "tone_a.wav", 440.0, 44100.0, 60.0);
  writeSineWav(music / "tone_b.wav", 660.0, 48000.0, 60.0);
  writeSineWav(music / "tone_c.wav", 550.0, 44100.0, 60.0);
  writeSineWav(music / "tone_d.wav", 770.0, 48000.0, 60.0);
  app.library()->requestScan(music.string());
  REQUIRE(waitFor([&] { return app.library()->listAll().size() == 4 && !app.library()->scanStatus().scanning; }));
  REQUIRE(waitFor([&] {
    const auto all = app.library()->listAll();
    return std::all_of(all.begin(), all.end(),
                       [](const core::TrackItem& t) { return t.analysisTotal > 0 && t.analysisDone == t.analysisTotal; });
  }, 60s));

  // Replace whatever the analysis made of the synthetic tones with known tempo, grid and energy.
  auto db = library::Database::open(root / "data" / "library.db");
  for (const auto& track : app.library()->listAll()) {
    const bool is44k = track.filepath.find("tone_a") != std::string::npos ||
                       track.filepath.find("tone_c") != std::string::npos;
    auto record = library::LibraryRepository::findTrackById(db, track.id);
    REQUIRE(record.has_value());
    record->bpm = 120.0;
    record->energy = 7.0;
    library::LibraryRepository::updateTrack(db, *record);
    library::BeatgridRecord grid;
    grid.trackId = track.id;
    grid.bpm = 120.0;
    grid.firstBeatFrame = static_cast<std::int64_t>(0.25 * (is44k ? 44100.0 : 48000.0));
    grid.source = "user";
    library::LibraryRepository::saveBeatgrid(db, grid);
  }
}

}  // namespace

TEST_CASE("App composition: scan, load two tracks, play and mix through the real engine", "[app][integration]") {
  juce::ScopedJuceInitialiser_GUI juceInit;

  const auto root = std::filesystem::temp_directory_path() / "zyron_app_composition_test";
  std::filesystem::remove_all(root);
  const auto music = root / "music";
  std::filesystem::create_directories(music);
  writeSineWav(music / "tone_a.wav", 440.0, 44100.0, 4.0);
  writeSineWav(music / "tone_b.wav", 660.0, 48000.0, 4.0);

  {
    application::ApplicationComposition app(root / "data");
    REQUIRE(app.libraryError().empty());
    REQUIRE(app.library() != nullptr);
    auto& engine = *app.engine();
    engine.audioDeviceAboutToStart(nullptr);  // 48 kHz default, no device

    const auto scanned = [&] {
      return app.library()->listAll().size() == 2 && !app.library()->scanStatus().scanning;
    };

    SECTION("a scanned folder fills the library") {
      app.library()->requestScan(music.string());
      REQUIRE(waitFor(scanned));
      for (const auto& track : app.library()->listAll()) {
        CHECK(track.id > 0);
        CHECK(track.durationSec > 3.5);
        CHECK(track.durationSec < 4.5);
      }
    }

    SECTION("load, play, EQ, crossfader and seek all reach the audio") {
      app.library()->requestScan(music.string());
      REQUIRE(waitFor(scanned));
      const auto tracks = app.library()->listAll();
      const auto idA = core::TrackId{tracks[0].id};
      const auto idB = core::TrackId{tracks[1].id};

      submit(app, core::LoadTrack{core::DeckId::A, idA});
      submit(app, core::LoadTrack{core::DeckId::B, idB});
      REQUIRE(waitFor([&] {
        return engine.loadStatus(core::DeckId::A).phase == core::DeckLoadPhase::Ready &&
               engine.loadStatus(core::DeckId::B).phase == core::DeckLoadPhase::Ready;
      }));

      const auto statusA = engine.loadStatus(core::DeckId::A);
      REQUIRE(statusA.waveform != nullptr);
      CHECK_FALSE(statusA.waveform->empty());

      // Idle decks are silent.
      CHECK(renderRms(engine, 8) < 1.0e-4);

      submit(app, core::Play{core::DeckId::A});
      submit(app, core::Play{core::DeckId::B});
      const double playing = renderRms(engine, 40);
      CHECK(playing > 0.05);

      const auto live = engine.liveState();
      CHECK(live.decks[0].isPlaying);
      CHECK(live.decks[0].positionSec > 0.05);
      CHECK(live.decks[0].durationSec > 3.5);
      CHECK(live.decks[0].peakLeft > 0.01F);

      // Crossfader hard right: deck A (left side) must vanish, B stays.
      submit(app, core::SetCrossfader{1.0F});
      (void)renderRms(engine, 40);  // let the smoothing settle
      const double rightOnly = renderRms(engine, 20);
      CHECK(rightOnly > 0.02);

      // With B paused only A could still be heard, and the crossfader has cut it: silence.
      submit(app, core::Pause{core::DeckId::B});
      (void)renderRms(engine, 40);
      CHECK(renderRms(engine, 20) < 0.005);
      submit(app, core::Play{core::DeckId::B});
      (void)renderRms(engine, 40);

      // Killing the mid band of deck B (660 Hz sits in the mids) silences what is left.
      submit(app, core::SetEq{core::DeckId::B, core::EqBand::Mid, -60.0F});
      (void)renderRms(engine, 80);
      CHECK(renderRms(engine, 20) < rightOnly * 0.1);

      // Seek moves the playhead.
      submit(app, core::Seek{core::DeckId::A, 2.5});
      (void)renderRms(engine, 4);
      CHECK(engine.liveState().decks[0].positionSec > 2.4);

      // Pause stops the clock.
      submit(app, core::Pause{core::DeckId::A});
      (void)renderRms(engine, 20);
      const double stopped = engine.liveState().decks[0].positionSec;
      (void)renderRms(engine, 20);
      CHECK(engine.liveState().decks[0].positionSec == stopped);
    }

    SECTION("sync matches tempo and aligns the beats of two decks") {
      app.library()->requestScan(music.string());
      REQUIRE(waitFor(scanned));
      const auto tracks = app.library()->listAll();

      // Known grids written as the user's own edit, which the analysis never overwrites. tone_a plays on deck A
      // (44.1 kHz, 120 BPM), tone_b on deck B (48 kHz, 126 BPM).
      std::int64_t idA = 0;
      std::int64_t idB = 0;
      {
        auto db = library::Database::open(root / "data" / "library.db");
        for (const auto& track : tracks) {
          const bool isA = track.filepath.find("tone_a") != std::string::npos;
          (isA ? idA : idB) = track.id;
          const double rate = isA ? 44100.0 : 48000.0;
          library::BeatgridRecord grid;
          grid.trackId = track.id;
          grid.bpm = isA ? 120.0 : 126.0;
          grid.firstBeatFrame = static_cast<std::int64_t>((isA ? 0.25 : 0.10) * rate);
          grid.source = "user";
          library::LibraryRepository::saveBeatgrid(db, grid);
        }
      }
      REQUIRE(idA > 0);
      REQUIRE(idB > 0);

      submit(app, core::LoadTrack{core::DeckId::A, core::TrackId{idA}});
      submit(app, core::LoadTrack{core::DeckId::B, core::TrackId{idB}});
      REQUIRE(waitFor([&] {
        return engine.loadStatus(core::DeckId::A).phase == core::DeckLoadPhase::Ready &&
               engine.loadStatus(core::DeckId::B).phase == core::DeckLoadPhase::Ready;
      }));
      submit(app, core::Play{core::DeckId::A});
      (void)renderRms(engine, 100);

      submit(app, core::Sync{core::DeckId::B});
      submit(app, core::Play{core::DeckId::B});
      (void)renderRms(engine, 60);

      const auto live = engine.liveState();
      CHECK(live.notice.empty());
      CHECK(std::abs(live.decks[1].playbackSpeed - 120.0 / 126.0) < 1.0e-3);

      const auto beatFraction = [](double seconds, double bpm, double first) {
        const double beats = (seconds - first) * bpm / 60.0;
        return beats - std::floor(beats);
      };
      const double fracA = beatFraction(live.decks[0].positionSec, 120.0, 0.25);
      const double fracB = beatFraction(live.decks[1].positionSec, 126.0, 0.10);
      double diff = std::abs(fracA - fracB);
      diff = std::min(diff, 1.0 - diff);
      INFO("posA=" << live.decks[0].positionSec << " posB=" << live.decks[1].positionSec << " fracA=" << fracA << " fracB=" << fracB << " speedB=" << live.decks[1].playbackSpeed);
      CHECK(diff < 0.03);  // within 3 % of a beat
    }

    SECTION("automix plans a set, loads, mixes and finishes it") {
      prepareSyntheticSet(app, music, root);

      auto* automix = app.automix();
      REQUIRE(automix != nullptr);
      REQUIRE(automix->start());

      bool bothPlayed = false;
      core::AutonomousDjTelemetry telemetry;
      for (int block = 0; block < 60000; ++block) {
        (void)renderRms(engine, 1);
        if (block % 19 != 0) {
          continue;
        }
        std::this_thread::sleep_for(1ms);  // lets the loader thread keep up with the fast-forwarded clock
        automix->tick();
        const auto live = engine.liveState();
        int playing = 0;
        for (const auto& deck : live.decks) {
          playing += deck.isPlaying ? 1 : 0;
        }
        bothPlayed = bothPlayed || playing >= 2;
        telemetry = automix->telemetry();
        if (telemetry.status != core::AutonomousDjStatus::Running) {
          break;
        }
      }
      INFO(telemetry.statusMessage);
      CHECK(telemetry.status == core::AutonomousDjStatus::Finished);
      CHECK(bothPlayed);  // during the transition the outgoing and the incoming deck play together
      CHECK(telemetry.totalTracksInSet >= 2);
      CHECK(telemetry.currentTrackIndex == telemetry.totalTracksInSet);
    }

    SECTION("neural stem separation attaches stems to the deck (skipped without weights)") {
      if (!app.models().available()) {
        SUCCEED("model weights not found: run scripts/download_models.ps1");
      } else {
        app.library()->requestScan(music.string());
        REQUIRE(waitFor(scanned));
        const auto tracks = app.library()->listAll();
        const auto track = core::TrackId{tracks[0].id};

        submit(app, core::LoadTrack{core::DeckId::A, track});
        REQUIRE(waitFor([&] { return engine.loadStatus(core::DeckId::A).phase == core::DeckLoadPhase::Ready; }));
        CHECK_FALSE(engine.liveState().decks[0].hasStems);

        submit(app, core::SeparateStems{core::DeckId::A});
        REQUIRE(waitFor(
            [&] {
              const auto status = engine.loadStatus(core::DeckId::A);
              return status.stemPhase == core::StemPhase::Ready || status.stemPhase == core::StemPhase::Failed;
            },
            300s));
        const auto status = engine.loadStatus(core::DeckId::A);
        INFO(status.stemMessage);
        REQUIRE(status.stemPhase == core::StemPhase::Ready);

        submit(app, core::Play{core::DeckId::A});
        const double withStems = renderRms(engine, 80);
        CHECK(engine.liveState().decks[0].hasStems);
        CHECK(withStems > 0.05);  // the stems add up to roughly the original

        // Muting all four stems silences the deck: it really plays from the stems now.
        for (const auto stem : {core::StemKind::Vocals, core::StemKind::Drums, core::StemKind::Bass, core::StemKind::Other}) {
          submit(app, core::SetStemMute{core::DeckId::A, stem, true});
        }
        (void)renderRms(engine, 40);
        CHECK(renderRms(engine, 40) < withStems * 0.05);
        submit(app, core::Pause{core::DeckId::A});

        // Loading the same track again attaches the cached stems by itself.
        submit(app, core::LoadTrack{core::DeckId::B, track});
        REQUIRE(waitFor([&] { return engine.loadStatus(core::DeckId::B).stemPhase == core::StemPhase::Ready; }, 60s));
        (void)renderRms(engine, 4);  // telemetry is published by the next audio block
        CHECK(engine.liveState().decks[1].hasStems);
      }
    }

    SECTION("markers: the user sets mix points and drops, they persist and are listed in time order") {
      app.library()->requestScan(music.string());
      REQUIRE(waitFor(scanned));
      const auto tracks = app.library()->listAll();
      const std::int64_t id = tracks[0].id;

      core::TrackMarker drop;
      drop.type = core::TrackMarker::kDrop;
      drop.timeSec = 2.5;
      core::TrackMarker out;
      out.type = core::TrackMarker::kMixOut;
      out.timeSec = 1.0;
      const int dropId = app.library()->setMarker(id, drop);
      const int outId = app.library()->setMarker(id, out);
      CHECK(dropId >= library::LibraryService::kFirstMarkerIndex);
      CHECK(outId > dropId);

      auto markers = app.library()->markers(id);
      const auto userMarkers = std::count_if(markers.begin(), markers.end(),
                                             [](const core::TrackMarker& m) { return m.source == "user"; });
      CHECK(userMarkers == 2);
      std::vector<core::TrackMarker> mine;
      for (const auto& m : markers) {
        if (m.source == "user") mine.push_back(m);
      }
      REQUIRE(mine.size() == 2);
      CHECK(mine[0].type == core::TrackMarker::kMixOut);  // ordered by time
      CHECK(mine[0].timeSec > 0.99);
      CHECK(mine[0].timeSec < 1.01);
      CHECK(mine[1].type == core::TrackMarker::kDrop);

      // Moving a marker keeps its id; removing it deletes it.
      drop.id = dropId;
      drop.timeSec = 3.0;
      CHECK(app.library()->setMarker(id, drop) == dropId);
      app.library()->removeMarker(id, outId);
      markers = app.library()->markers(id);
      CHECK(std::none_of(markers.begin(), markers.end(), [&](const core::TrackMarker& m) { return m.id == outId; }));
      CHECK(std::any_of(markers.begin(), markers.end(),
                        [&](const core::TrackMarker& m) { return m.id == dropId && m.timeSec > 2.99; }));
    }

    SECTION("track status: the library reports how far the preparation of each track has got") {
      app.library()->requestScan(music.string());
      REQUIRE(waitFor(scanned));
      const auto allPrepared = [&] {
        const auto all = app.library()->listAll();
        return std::all_of(all.begin(), all.end(),
                           [](const core::TrackItem& t) { return t.analysisTotal > 0 && t.analysisDone == t.analysisTotal; });
      };
      REQUIRE(waitFor(allPrepared, 60s));  // a task that is running counts as not done yet
      for (const auto& track : app.library()->listAll()) {
        CHECK(track.analysisTotal >= 7);  // duration, waveform, bpm, beatgrid, key, energy, structure
      }
    }

    SECTION("automix follows the markers and shows its queue") {
      prepareSyntheticSet(app, music, root);

      // Every track: mix out at 45 s, drop at 20 s. The transition then starts at 45 s of the playing track, and the
      // incoming track starts so that its drop lands in the middle of the 32 s transition: 20 - 16 = 4 s.
      for (const auto& track : app.library()->listAll()) {
        core::TrackMarker mixOut;
        mixOut.type = core::TrackMarker::kMixOut;
        mixOut.timeSec = 45.0;
        core::TrackMarker drop;
        drop.type = core::TrackMarker::kDrop;
        drop.timeSec = 20.0;
        REQUIRE(app.library()->setMarker(track.id, mixOut) > 0);
        REQUIRE(app.library()->setMarker(track.id, drop) > 0);
      }

      auto* automix = app.automix();
      REQUIRE(automix != nullptr);
      REQUIRE(automix->start());

      // The queue shows the whole plan with the points the mix will use.
      auto queue = automix->queue();
      REQUIRE(queue.entries.size() == 4);
      CHECK(queue.running);
      CHECK(queue.current == 0);
      CHECK_THAT(queue.entries[0].points.mixOutSec, WithinAbs(45.0, 0.1));
      CHECK_THAT(queue.entries[1].points.dropSec, WithinAbs(20.0, 0.1));
      CHECK_FALSE(queue.entries[2].reason.empty());

      // Tracks that have not started can be reordered and removed; the playing and the next one are fixed once loading.
      const auto thirdId = queue.entries[2].track.id;
      const auto fourthId = queue.entries[3].track.id;
      CHECK(automix->moveUpcoming(3, 2));
      queue = automix->queue();
      CHECK(queue.entries[2].track.id == fourthId);
      CHECK(queue.entries[3].track.id == thirdId);
      CHECK_FALSE(automix->moveUpcoming(0, 2));  // the playing track cannot be moved
      CHECK(automix->removeUpcoming(3));
      CHECK(automix->queue().entries.size() == 3);

      // Run until both decks play together and look at where each of them is.
      double outgoingAt = -1.0;
      double incomingAt = -1.0;
      for (int block = 0; block < 60000 && outgoingAt < 0.0; ++block) {
        (void)renderRms(engine, 1);
        if (block % 19 != 0) {
          continue;
        }
        std::this_thread::sleep_for(1ms);
        automix->tick();
        const auto live = engine.liveState();
        int playing = 0;
        for (const auto& deck : live.decks) {
          playing += deck.isPlaying ? 1 : 0;
        }
        if (playing >= 2) {
          const auto active = automix->telemetry().activeDeck;
          const auto other = static_cast<core::DeckId>((core::index(active) + 1) % 2);
          outgoingAt = live.decks[core::index(active)].positionSec;
          incomingAt = live.decks[core::index(other)].positionSec;
        }
      }
      INFO(automix->telemetry().statusMessage);
      CHECK(outgoingAt > 44.0);   // the mix out began at the marker, not at the default lead time (30 s before the end)
      CHECK(outgoingAt < 47.0);
      CHECK(incomingAt > 3.5);    // the incoming track started at its drop minus half the transition
      CHECK(incomingAt < 6.5);
      automix->stop();
    }

    SECTION("scrubbing does not stop automix; jump to transition; mix next while automix runs") {
      prepareSyntheticSet(app, music, root);
      for (const auto& track : app.library()->listAll()) {
        core::TrackMarker mixOut;
        mixOut.type = core::TrackMarker::kMixOut;
        mixOut.timeSec = 45.0;
        REQUIRE(app.library()->setMarker(track.id, mixOut) > 0);
      }
      auto* automix = app.automix();
      REQUIRE(automix != nullptr);
      REQUIRE(automix->start());

      const auto step = [&](int blocks) {
        for (int block = 0; block < blocks; ++block) {
          (void)renderRms(engine, 1);
          if (block % 19 == 0) {
            std::this_thread::sleep_for(1ms);
            automix->tick();
          }
        }
      };
      step(400);
      const auto active = automix->telemetry().activeDeck;
      const auto other = static_cast<core::DeckId>((core::index(active) + 1) % 2);

      // The next track is preloaded on the free deck as soon as the first one plays (waveform and markers ready).
      // Decoding runs on the loader thread in real time, so give it a moment.
      for (int wait = 0; wait < 500 && engine.loadStatus(other).phase != core::DeckLoadPhase::Ready; ++wait) {
        std::this_thread::sleep_for(10ms);
        automix->tick();
      }
      CHECK(engine.loadStatus(other).phase == core::DeckLoadPhase::Ready);

      // The DJ scrubs around the playing deck with the UI: that must not take the mix away from the AI.
      const core::CommandOrigin ui{core::CommandOrigin::Kind::Ui, "test-ui"};
      REQUIRE_FALSE(app.bus().submit(core::Seek{active, 30.0}, ui).has_value());
      step(200);
      CHECK(automix->telemetry().status == core::AutonomousDjStatus::Running);
      CHECK(automix->telemetry().statusMessage.find("manual control") == std::string::npos);

      // Jump to the planned transition: 12 s before the mix-out marker at 45 s.
      CHECK(automix->jumpToTransition());
      step(8);
      const double position = engine.liveState().decks[core::index(active)].positionSec;
      CHECK(position > 32.0);
      CHECK(position < 36.0);

      // Live "play this next": the chosen track replaces the next one and the mix starts at once.
      const auto queue = automix->queue();
      REQUIRE(queue.entries.size() >= 3);
      const auto chosen = queue.entries.back().track.id;
      CHECK(automix->mixNext(chosen).empty());
      bool bothPlayed = false;
      for (int block = 0; block < 30000 && !bothPlayed; ++block) {
        (void)renderRms(engine, 1);
        if (block % 19 == 0) {
          std::this_thread::sleep_for(1ms);
          automix->tick();
          int playing = 0;
          for (const auto& deck : engine.liveState().decks) {
            playing += deck.isPlaying ? 1 : 0;
          }
          bothPlayed = playing >= 2;
        }
      }
      CHECK(bothPlayed);
      CHECK(engine.loadStatus(other).track.value == chosen);
      automix->stop();
    }

    SECTION("live mix without automix: the playing deck is mixed into the chosen track") {
      prepareSyntheticSet(app, music, root);
      const auto tracks = app.library()->listAll();
      auto* automix = app.automix();
      REQUIRE(automix != nullptr);

      submit(app, core::LoadTrack{core::DeckId::A, core::TrackId{tracks[0].id}});
      REQUIRE(waitFor([&] { return engine.loadStatus(core::DeckId::A).phase == core::DeckLoadPhase::Ready; }));
      submit(app, core::Play{core::DeckId::A});
      (void)renderRms(engine, 60);

      CHECK(automix->mixNext(9999) == "The track is not in the library");
      CHECK(automix->mixNext(tracks[1].id).empty());
      CHECK_FALSE(automix->liveMixStatus().empty());

      bool bothPlayed = false;
      for (int block = 0; block < 40000; ++block) {
        (void)renderRms(engine, 1);
        if (block % 19 != 0) {
          continue;
        }
        std::this_thread::sleep_for(1ms);
        automix->tick();
        const auto live = engine.liveState();
        bothPlayed = bothPlayed || (live.decks[0].isPlaying && live.decks[1].isPlaying);
        if (automix->liveMixStatus().empty() || automix->liveMixStatus().rfind("Now playing", 0) == 0) {
          break;
        }
      }
      (void)renderRms(engine, 8);
      const auto live = engine.liveState();
      CHECK(bothPlayed);                // during the transition both decks play
      CHECK_FALSE(live.decks[0].isPlaying);  // afterwards the old deck is paused
      CHECK(live.decks[1].isPlaying);
    }

    SECTION("sync without a beat grid says why") {
      app.library()->requestScan(music.string());
      REQUIRE(waitFor(scanned));
      const auto tracks = app.library()->listAll();
      submit(app, core::LoadTrack{core::DeckId::A, core::TrackId{tracks[0].id}});
      REQUIRE(waitFor([&] { return engine.loadStatus(core::DeckId::A).phase == core::DeckLoadPhase::Ready; }));
      submit(app, core::Sync{core::DeckId::A});
      const auto live = engine.liveState();
      CHECK_FALSE(live.notice.empty());
    }

    SECTION("a missing file is reported, not played") {
      app.library()->requestScan(music.string());
      REQUIRE(waitFor(scanned));
      const auto tracks = app.library()->listAll();
      std::filesystem::remove(toPath(tracks[0].filepath));

      submit(app, core::LoadTrack{core::DeckId::C, core::TrackId{tracks[0].id}});
      REQUIRE(waitFor([&] { return engine.loadStatus(core::DeckId::C).phase == core::DeckLoadPhase::Failed; }));
      CHECK_FALSE(engine.loadStatus(core::DeckId::C).message.empty());
    }

    SECTION("real music from ZYRON_TEST_MUSIC_DIR (optional, skipped when unset)") {
      const juce::String musicDir = juce::SystemStats::getEnvironmentVariable("ZYRON_TEST_MUSIC_DIR", "");
      if (musicDir.isEmpty()) {
        SUCCEED("ZYRON_TEST_MUSIC_DIR not set");
      } else {
        app.library()->requestScan(musicDir.toStdString());
        REQUIRE(waitFor([&] { return !app.library()->listAll().empty() && !app.library()->scanStatus().scanning; },
                        120s));
        const auto tracks = app.library()->listAll();
        INFO("tracks found: " << tracks.size());
        REQUIRE(tracks.size() >= 2);
        for (const auto& track : tracks) {
          CHECK(track.durationSec > 10.0);  // tags and MPEG headers gave a real duration
        }

        // Background analysis fills in tempo, grid, key and energy.
        REQUIRE(waitFor([&] { return app.library()->scanStatus().analysisPending == 0; }, 600s));
        int withBpm = 0;
        for (const auto& track : app.library()->listAll()) {
          const auto detail = app.library()->findTrack(track.id);
          REQUIRE(detail.has_value());
          INFO(track.title << " bpm=" << detail->bpm << " key=" << track.key << " energy=" << track.energy);
          if (detail->bpm > 0.0) {
            ++withBpm;
            CHECK(detail->bpm > 60.0);
            CHECK(detail->bpm < 200.0);
            CHECK(detail->firstBeatSec >= 0.0);
            CHECK(detail->firstBeatSec < track.durationSec);  // the grid anchor lies inside the track
          }
          std::printf("  %-40.40s bpm=%7.2f first=%5.2fs key=%s energy=%.1f\n", track.title.c_str(), detail->bpm,
                      detail->firstBeatSec, track.key.c_str(), track.energy);
        }
        CHECK(withBpm >= static_cast<int>(tracks.size()) * 3 / 4);  // most tracks get a tempo

        // The AI proposes the structure of each track: intro, drops, breakdowns, outro and the mix points.
        REQUIRE(waitFor([&] {
          const auto all = app.library()->listAll();
          return std::all_of(all.begin(), all.end(),
                             [](const core::TrackItem& t) { return t.analysisTotal > 0 && t.analysisDone == t.analysisTotal; });
        }, 300s));
        for (const auto& track : app.library()->listAll()) {
          INFO(track.title << ": " << track.analysisFailed << " of " << track.analysisTotal << " tasks failed");
          CHECK(track.analysisFailed == 0);  // MP3s included: no task may fail on a normal track
          CHECK_FALSE(track.waveformPeaksPath.empty());
        }
        int withMixPoints = 0;
        for (const auto& track : app.library()->listAll()) {
          const auto markers = app.library()->markers(track.id);
          std::string types;
          for (const auto& marker : markers) {
            types += marker.type + "@" + std::to_string(static_cast<int>(marker.timeSec)) + " ";
          }
          std::printf("  %-34.34s markers: %s\n", track.title.c_str(), types.c_str());
          const bool hasMixIn = std::any_of(markers.begin(), markers.end(), [](const core::TrackMarker& m) { return m.type == "mix_in"; });
          const bool hasMixOut = std::any_of(markers.begin(), markers.end(), [](const core::TrackMarker& m) { return m.type == "mix_out"; });
          withMixPoints += (hasMixIn && hasMixOut) ? 1 : 0;
        }
        CHECK(withMixPoints >= 1);

        // Automix over the real library: fast-forward until the first transition has completed.
        {
          auto* automix = app.automix();
          REQUIRE(automix != nullptr);
          REQUIRE(automix->start());
          bool bothPlayed = false;
          core::AutonomousDjTelemetry telemetry;
          double maxDeckPeak = 0.0;
          for (int block = 0; block < 200000; ++block) {
            (void)renderRms(engine, 1);
            if (block % 19 != 0) {
              continue;
            }
            std::this_thread::sleep_for(1ms);
            automix->tick();
            const auto live = engine.liveState();
            int playing = 0;
            for (const auto& deck : live.decks) {
              playing += deck.isPlaying ? 1 : 0;
              maxDeckPeak = std::max<double>(maxDeckPeak, std::max(deck.peakLeft, deck.peakRight));
            }
            bothPlayed = bothPlayed || playing >= 2;
            telemetry = automix->telemetry();
            if (telemetry.status != core::AutonomousDjStatus::Running || telemetry.currentTrackIndex >= 2) {
              break;
            }
          }
          std::printf("  automix: %s | track %zu of %zu\n", telemetry.statusMessage.c_str(),
                      telemetry.currentTrackIndex, telemetry.totalTracksInSet);
          CHECK(telemetry.status == core::AutonomousDjStatus::Running);
          CHECK(telemetry.currentTrackIndex >= 2);
          CHECK(bothPlayed);
          CHECK(maxDeckPeak > 0.05);
          automix->stop();
        }

        submit(app, core::LoadTrack{core::DeckId::A, core::TrackId{tracks[0].id}});
        submit(app, core::LoadTrack{core::DeckId::B, core::TrackId{tracks[1].id}});
        REQUIRE(waitFor(
            [&] {
              return engine.loadStatus(core::DeckId::A).phase != core::DeckLoadPhase::Loading &&
                     engine.loadStatus(core::DeckId::B).phase != core::DeckLoadPhase::Loading;
            },
            120s));
        const auto a = engine.loadStatus(core::DeckId::A);
        INFO("deck A: " << a.message);
        REQUIRE(a.phase == core::DeckLoadPhase::Ready);
        REQUIRE(a.waveform != nullptr);
        CHECK(a.waveform->durationSec() > 10.0);

        submit(app, core::Play{core::DeckId::A});
        (void)renderRms(engine, 20);
        CHECK(renderRms(engine, 200) > 0.01);
        CHECK(engine.liveState().decks[0].positionSec > 0.5);
      }
    }

    SECTION("loading an unknown track id fails cleanly") {
      submit(app, core::LoadTrack{core::DeckId::D, core::TrackId{9999}});
      CHECK(engine.loadStatus(core::DeckId::D).phase == core::DeckLoadPhase::Failed);
    }
  }
  std::filesystem::remove_all(root);
}

namespace {

/// 40 s at 170 BPM: sub bass and kicks all the way, a sung line (moving harmonic voice) from 12 s to 30 s.
void writeSungTrack(const std::filesystem::path& path, double sampleRate) {
  const double seconds = 40.0;
  const auto frames = static_cast<std::int64_t>(seconds * sampleRate);
  audio::TrackBuffer buffer(2, frames, sampleRate);
  static constexpr double kVowels[5][2] = {{700, 1200}, {400, 2000}, {300, 2500}, {500, 900}, {350, 700}};
  const double beat = 60.0 / 170.0;
  double phase[21] = {};
  for (std::int64_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    const double inBeat = std::fmod(t, beat);
    double sample = inBeat < 0.12 ? 0.5 * std::exp(-inBeat * 30.0) * std::sin(2.0 * std::numbers::pi * 60.0 * inBeat * (1.0 + 2.0 * std::exp(-inBeat * 40.0)))
                                  : 0.0;
    sample += 0.25 * std::sin(2.0 * std::numbers::pi * 55.0 * t);
    if (t >= 12.0 && t < 30.0) {
      const int vowel = static_cast<int>(t / 0.3) % 5;
      const double f0 = 200.0 * (1.0 + 0.12 * std::sin(2.0 * std::numbers::pi * 0.7 * t));
      const double syllable = 0.55 + 0.45 * std::sin(2.0 * std::numbers::pi * 3.5 * t);
      double voice = 0.0;
      for (int h = 1; h <= 20; ++h) {
        phase[h] += 2.0 * std::numbers::pi * f0 * h / sampleRate;
        const double f = f0 * h;
        const double g = std::exp(-std::pow((f - kVowels[vowel][0]) / 150.0, 2.0)) +
                         0.7 * std::exp(-std::pow((f - kVowels[vowel][1]) / 250.0, 2.0)) + 0.02;
        voice += g * std::sin(phase[h]) / h;
      }
      sample += 0.35 * syllable * voice;
    }
    buffer.channelData(0)[i] = static_cast<float>(sample);
    buffer.channelData(1)[i] = static_cast<float>(sample);
  }
  REQUIRE(audio::WavDecoder::encode(path, buffer, true));
}

}  // namespace

TEST_CASE("Analysis: loudness, bar energy profile and vocal regions, from the analyzer to the sidecar to the mix points",
          "[app][integration][analysis]") {
  juce::ScopedJuceInitialiser_GUI juceInit;
  const auto root = std::filesystem::temp_directory_path() / "zyron_app_analysis_extras_test";
  std::filesystem::remove_all(root);
  const auto music = root / "music";
  std::filesystem::create_directories(music);
  const auto file = music / "sung.wav";
  writeSungTrack(file, 44100.0);

  SECTION("the analyzer measures loudness, bar profile and vocals; the sidecar keeps them; the cheap path redoes them") {
    application::TrackAnalysis result;
    std::string error;
    const auto started = std::chrono::steady_clock::now();
    REQUIRE(application::TrackAnalyzer::analyze(file, "drum and bass", result, error));
    const double analysisMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    INFO("full analysis of 40 s: " << analysisMs << " ms");
    REQUIRE(result.loudnessOk);
    CHECK(result.loudnessLufs < -5.0);
    CHECK(result.loudnessLufs > -40.0);
    REQUIRE(result.barProfileOk);
    CHECK(result.barProfile.energy.size() > 10);
    CHECK(result.vocalMethod == "DSP");
    int starts = 0;
    int ends = 0;
    for (const auto& marker : result.markers) {
      starts += marker.type == core::TrackMarker::kVocal ? 1 : 0;
      ends += marker.type == core::TrackMarker::kVocalEnd ? 1 : 0;
    }
    CHECK(starts >= 1);
    CHECK(starts == ends);

    application::AnalysisSidecar sidecar([&](const std::filesystem::path&) { return music; });
    sidecar.store(file, "hash-sung", 2, 6, result);
    application::TrackAnalysis loaded;
    int storedStructure = 0;
    REQUIRE(sidecar.load(file, "hash-sung", 2, -1, loaded, &storedStructure));
    CHECK(storedStructure == 6);
    CHECK(loaded.loudnessOk);
    CHECK_THAT(loaded.loudnessLufs, WithinAbs(result.loudnessLufs, 1e-9));
    REQUIRE(loaded.barProfileOk);
    CHECK(loaded.barProfile.energy.size() == result.barProfile.energy.size());
    CHECK(loaded.vocalMethod == "DSP");
    CHECK(loaded.markers.size() == result.markers.size());

    // An entry from before the new fields (no loudness, no profile) loads without them and is refreshed by the cheap
    // path, which keeps tempo, key and energy.
    application::TrackAnalysis old = loaded;
    old.loudnessOk = false;
    old.loudnessLufs = 0.0;
    old.barProfileOk = false;
    old.barProfile = {};
    old.vocalMethod.clear();
    old.markers.clear();
    const double bpm = old.bpm;
    REQUIRE(application::TrackAnalyzer::reproposeMarkers(file, old, error));
    CHECK(old.bpm == bpm);
    CHECK(old.loudnessOk);
    CHECK_THAT(old.loudnessLufs, WithinAbs(result.loudnessLufs, 0.05));
    CHECK(old.barProfileOk);
    CHECK(old.vocalMethod == "DSP");
  }

  SECTION("a scanned track gets loudness and vocals that the Automix reads as mix points") {
    application::ApplicationComposition app(root / "data");
    REQUIRE(app.libraryError().empty());
    app.library()->requestScan(music.string());
    REQUIRE(waitFor([&] { return app.library()->listAll().size() == 1 && !app.library()->scanStatus().scanning; }));
    REQUIRE(waitFor([&] {
      const auto all = app.library()->listAll();
      return !all.empty() && all[0].analysisTotal > 0 && all[0].analysisDone == all[0].analysisTotal;
    }, 120s));
    const auto id = app.library()->listAll()[0].id;
    const core::TrackMixPoints points = app.automix()->mixPoints(id);
    CHECK(points.loudnessLufs < 0.0);
    CHECK(points.barSec > 0.0);
    CHECK(points.barEnergy.size() > 10);
    CHECK(points.barBassEnergy.size() == points.barEnergy.size());
    REQUIRE_FALSE(points.vocals.empty());
    CHECK(points.vocals.front().first < points.vocals.front().second);
    CHECK(points.vocals.front().first >= 8.0);
    CHECK(points.vocals.back().second <= 34.0);
  }
}
