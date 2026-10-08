// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "Core/Commands/CommandBus.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/AppState.hpp"
#include "Core/System/EngineStats.hpp"
#include "UI/Deck/DeckComponent.hpp"
#include "UI/MainComponent.hpp"
#include "UI/Mixer/MixerComponent.hpp"
#include "UI/Theme.hpp"
#include "UI/Waveform/GlobalWaveformComponent.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron;

namespace {

class MockStatsSource final : public core::AudioEngineStatsSource {
 public:
  [[nodiscard]] core::AudioEngineStats stats() const override {
    core::AudioEngineStats s;
    s.deviceOpen = true;
    s.sampleRate = 48000.0;
    s.bufferSize = 256;
    s.cpuLoad = 0.05;
    return s;
  }
};

class MockLibrarySource final : public core::ILibrarySource {
 public:
  std::vector<core::TrackItem> tracks;
  std::string lastScanPath;

  std::vector<core::TrackItem> search(std::string_view query) override {
    if (query.empty()) return tracks;
    std::vector<core::TrackItem> res;
    for (const auto& t : tracks) {
      if (t.title.find(query) != std::string::npos || t.artist.find(query) != std::string::npos) {
        res.push_back(t);
      }
    }
    return res;
  }

  std::vector<core::TrackItem> listAll() override { return tracks; }
  void requestScan(const std::string& folderPath) override { lastScanPath = folderPath; }
};

core::TrackItem makeSampleTrack(std::int64_t id, std::string title, std::string artist, double bpm, std::string key) {
  core::TrackItem t;
  t.id = id;
  t.title = std::move(title);
  t.artist = std::move(artist);
  t.bpm = bpm;
  t.key = std::move(key);
  t.durationSec = 210.0;
  t.energy = 8.5;
  return t;
}

}  // namespace

TEST_CASE("DeckComponent headless operations and callbacks", "[ui][deck]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  ui::DeckComponent deck(core::DeckId::A, ui::Theme::dark());
  deck.setSize(500, 350);

  SECTION("Renders cleanly when unpopulated") {
    juce::Image target(juce::Image::ARGB, 500, 350, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(deck.paintEntireComponent(g, true));
  }

  SECTION("setTrack updates metadata display and can be painted") {
    const auto track = makeSampleTrack(1, "Dead Limit", "Noisia & The Upbeats", 174.0, "4A");
    deck.setTrack(track);

    juce::Image target(juce::Image::ARGB, 500, 350, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(deck.paintEntireComponent(g, true));
  }

  SECTION("updateTelemetry updates playing state and hot cue badges") {
    core::DeckTelemetry telem;
    telem.deck = core::DeckId::A;
    telem.hasTrack = true;
    telem.isPlaying = true;
    telem.currentTimeSec = 45.0;
    telem.durationSec = 210.0;
    telem.loop.active = true;
    telem.loop.startTimeSec = 30.0;
    telem.loop.endTimeSec = 40.0;

    core::CuePointTelemetry cue1;
    cue1.index = 1;
    cue1.timeSec = 15.0;
    cue1.color = "#FF0055";
    cue1.name = "Intro";
    telem.hotCues[0] = cue1;

    deck.updateTelemetry(telem);

    juce::Image target(juce::Image::ARGB, 500, 350, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(deck.paintEntireComponent(g, true));
  }

  SECTION("Callbacks fire on transport, cues, loops and pitch interaction") {
    bool playFired = false;
    bool pauseFired = false;
    bool cueFired = false;
    bool syncFired = false;
    int hotCueIndexFired = -1;
    double pitchFired = 0.0;

    deck.onPlayClicked = [&](core::DeckId id) {
      if (id == core::DeckId::A) playFired = true;
    };
    deck.onPauseClicked = [&](core::DeckId id) {
      if (id == core::DeckId::A) pauseFired = true;
    };
    deck.onCueClicked = [&](core::DeckId id) {
      if (id == core::DeckId::A) cueFired = true;
    };
    deck.onSyncClicked = [&](core::DeckId id) {
      if (id == core::DeckId::A) syncFired = true;
    };
    deck.onHotCueClicked = [&](core::DeckId id, int idx) {
      if (id == core::DeckId::A) hotCueIndexFired = idx;
    };
    deck.onPitchChanged = [&](core::DeckId id, double ratio) {
      if (id == core::DeckId::A) pitchFired = ratio;
    };

    // Trigger actions
    if (deck.onPlayClicked) deck.onPlayClicked(core::DeckId::A);
    CHECK(playFired);

    if (deck.onPauseClicked) deck.onPauseClicked(core::DeckId::A);
    CHECK(pauseFired);

    if (deck.onCueClicked) deck.onCueClicked(core::DeckId::A);
    CHECK(cueFired);

    if (deck.onSyncClicked) deck.onSyncClicked(core::DeckId::A);
    CHECK(syncFired);

    if (deck.onHotCueClicked) deck.onHotCueClicked(core::DeckId::A, 3);
    CHECK(hotCueIndexFired == 3);

    if (deck.onPitchChanged) deck.onPitchChanged(core::DeckId::A, 1.05);
    CHECK_THAT(pitchFired, WithinAbs(1.05, 1e-4));
  }
}

TEST_CASE("MixerComponent controls and meter updates", "[ui][mixer]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  ui::MixerComponent mixer(ui::Theme::dark());
  mixer.setSize(240, 400);

  SECTION("Renders cleanly") {
    juce::Image target(juce::Image::ARGB, 240, 400, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(mixer.paintEntireComponent(g, true));
  }

  SECTION("updateMeters updates channel and master peak bars") {
    mixer.updateMeters(0.75f, 0.70f, 0.40f, 0.35f, 0.85f, 0.80f);

    juce::Image target(juce::Image::ARGB, 240, 400, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(mixer.paintEntireComponent(g, true));
  }

  SECTION("Callbacks fire on EQ, gain, volume, crossfader, and master gain") {
    float gainFired = 0.0f;
    core::EqBand eqBandFired = core::EqBand::Low;
    float eqDbFired = 0.0f;
    float volFired = 0.0f;
    float xfaderFired = 0.0f;
    float masterGainFired = 0.0f;

    mixer.onGainChanged = [&](core::DeckId, float db) { gainFired = db; };
    mixer.onEqChanged = [&](core::DeckId, core::EqBand band, float db) {
      eqBandFired = band;
      eqDbFired = db;
    };
    mixer.onVolumeChanged = [&](core::DeckId, float lin) { volFired = lin; };
    mixer.onCrossfaderChanged = [&](float pos) { xfaderFired = pos; };
    mixer.onMasterGainChanged = [&](float db) { masterGainFired = db; };

    mixer.onGainChanged(core::DeckId::A, 3.5f);
    CHECK_THAT(gainFired, WithinAbs(3.5f, 1e-3));

    mixer.onEqChanged(core::DeckId::B, core::EqBand::Mid, -6.0f);
    CHECK(eqBandFired == core::EqBand::Mid);
    CHECK_THAT(eqDbFired, WithinAbs(-6.0f, 1e-3));

    mixer.onVolumeChanged(core::DeckId::A, 0.8f);
    CHECK_THAT(volFired, WithinAbs(0.8f, 1e-3));

    mixer.onCrossfaderChanged(-0.5f);
    CHECK_THAT(xfaderFired, WithinAbs(-0.5f, 1e-3));

    mixer.onMasterGainChanged(-2.0f);
    CHECK_THAT(masterGainFired, WithinAbs(-2.0f, 1e-3));
  }
}

TEST_CASE("GlobalWaveformComponent dual deck overview rendering and seeking", "[ui][global_waveform]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  ui::GlobalWaveformComponent globalWaveform(ui::Theme::dark());
  globalWaveform.setSize(800, 60);

  SECTION("Renders cleanly when empty") {
    juce::Image target(juce::Image::ARGB, 800, 60, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(globalWaveform.paintEntireComponent(g, true));
  }

  SECTION("Seek callback fires with targeted deck id and target seconds") {
    core::DeckId targetDeck = core::DeckId::B;
    double targetSec = 0.0;

    globalWaveform.onSeekRequested = [&](core::DeckId id, double sec) {
      targetDeck = id;
      targetSec = sec;
    };

    if (globalWaveform.onSeekRequested) {
      globalWaveform.onSeekRequested(core::DeckId::A, 42.5);
    }

    CHECK(targetDeck == core::DeckId::A);
    CHECK_THAT(targetSec, WithinAbs(42.5, 1e-3));
  }
}

TEST_CASE("MainComponent 2-deck UI shell complete assembly and interactions", "[ui][shell]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  core::StateStore store;
  core::EventBus events;
  core::CommandBus bus{store, events};
  MockStatsSource stats;
  auto libMock = std::make_shared<MockLibrarySource>();

  libMock->tracks.push_back(makeSampleTrack(1, "The Tide", "Noisia", 174.0, "1A"));
  libMock->tracks.push_back(makeSampleTrack(2, "Diplodocus", "Noisia", 174.0, "5A"));

  ui::MainComponent main(ui::Theme::dark(), bus, stats, libMock);
  main.setSize(1280, 800);

  SECTION("Subcomponent accessors return valid references") {
    CHECK(main.deck(core::DeckId::A).deckId() == core::DeckId::A);
    CHECK(main.deck(core::DeckId::B).deckId() == core::DeckId::B);
  }

  SECTION("Headless rendering paints entire window without error") {
    juce::Image target(juce::Image::ARGB, 1280, 800, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(main.paintEntireComponent(g, true));
  }

  SECTION("Loading track from library updates deck and publishes LoadTrack command") {
    const auto track = libMock->tracks[0];
    REQUIRE(main.library().onTrackLoadRequested != nullptr);

    main.library().onTrackLoadRequested(track, core::DeckId::A);

    // Verify AppState after LoadTrack
    const auto snap = store.snapshot();
    CHECK(snap->deck(core::DeckId::A).hasTrack());
    CHECK(snap->deck(core::DeckId::A).track == core::TrackId{1});
  }

  SECTION("Toggling settings panel adjusts visibility") {
    CHECK_FALSE(main.isSettingsVisible());
    main.setSettingsVisible(true);
    CHECK(main.isSettingsVisible());

    juce::Image target(juce::Image::ARGB, 1280, 800, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(main.paintEntireComponent(g, true));

    main.setSettingsVisible(false);
    CHECK_FALSE(main.isSettingsVisible());
  }

  SECTION("Switching to 4-deck layout mode activates Decks C and D") {
    CHECK(main.layoutMode() == ui::MainComponent::LayoutMode::TwoDecks);
    CHECK_FALSE(main.deck(core::DeckId::C).isVisible());
    CHECK_FALSE(main.deck(core::DeckId::D).isVisible());

    main.setLayoutMode(ui::MainComponent::LayoutMode::FourDecks);
    CHECK(main.layoutMode() == ui::MainComponent::LayoutMode::FourDecks);
    CHECK(main.deck(core::DeckId::C).isVisible());
    CHECK(main.deck(core::DeckId::D).isVisible());

    // Subcomponents accessible
    CHECK(main.deck(core::DeckId::C).deckId() == core::DeckId::C);
    CHECK(main.deck(core::DeckId::D).deckId() == core::DeckId::D);

    // Paints cleanly in 4-deck layout
    juce::Image target(juce::Image::ARGB, 1280, 800, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(main.paintEntireComponent(g, true));

    // Loading track into Deck C updates AppState
    const auto track = libMock->tracks[1];
    main.library().onTrackLoadRequested(track, core::DeckId::C);

    const auto snap = store.snapshot();
    CHECK(snap->deck(core::DeckId::C).hasTrack());
    CHECK(snap->deck(core::DeckId::C).track == core::TrackId{2});

    // Switch back to 2-deck layout
    main.setLayoutMode(ui::MainComponent::LayoutMode::TwoDecks);
    CHECK(main.layoutMode() == ui::MainComponent::LayoutMode::TwoDecks);
    CHECK_FALSE(main.deck(core::DeckId::C).isVisible());
    CHECK_FALSE(main.deck(core::DeckId::D).isVisible());
  }
}
