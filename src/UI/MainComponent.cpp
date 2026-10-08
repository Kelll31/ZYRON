// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/MainComponent.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace zyron::ui {

namespace {

core::WaveformData generateDefaultWaveform(double durationSec, double bpm) {
  core::WaveformData wf;
  wf.sampleRate = 44100;
  wf.samplesPerFrame = 512;
  wf.channels = 2;

  const int numFrames = static_cast<int>(std::max(10.0, durationSec) * 44100.0 / 512.0);
  wf.overview.resize(static_cast<std::size_t>(numFrames));
  wf.detail.resize(static_cast<std::size_t>(numFrames));

  const double beatSec = (bpm > 20.0) ? (60.0 / bpm) : 0.5;
  for (int i = 0; i < numFrames; ++i) {
    const double t = static_cast<double>(i * 512) / 44100.0;
    const double beatPhase = std::fmod(t, beatSec) / beatSec;
    const float beatKick = (beatPhase < 0.2) ? static_cast<float>(1.0 - beatPhase / 0.2) : 0.0f;

    const float low = std::clamp(beatKick * 0.9f + 0.1f, 0.0f, 1.0f);
    const float mid = std::clamp(0.3f + 0.4f * static_cast<float>(std::sin(t * 8.0) * 0.5 + 0.5), 0.0f, 1.0f);
    const float high = std::clamp(0.2f + 0.3f * static_cast<float>(std::cos(t * 16.0) * 0.5 + 0.5), 0.0f, 1.0f);

    core::WaveformPoint pt;
    pt.minLeft = -std::max({low, mid, high});
    pt.maxLeft = std::max({low, mid, high});
    pt.minRight = pt.minLeft;
    pt.maxRight = pt.maxLeft;
    pt.lowEnergy = low;
    pt.midEnergy = mid;
    pt.highEnergy = high;

    wf.overview[static_cast<std::size_t>(i)] = pt;
    wf.detail[static_cast<std::size_t>(i)] = pt;
  }
  return wf;
}

}  // namespace

MainComponent::MainComponent(const Theme& theme, core::CommandBus& bus,
                             const core::AudioEngineStatsSource& stats,
                             std::shared_ptr<core::ILibrarySource> librarySource)
    : theme_(theme),
      bus_(bus),
      stats_(stats),
      globalWaveform_(theme),
      deckA_(core::DeckId::A, theme),
      deckB_(core::DeckId::B, theme),
      deckC_(core::DeckId::C, theme),
      deckD_(core::DeckId::D, theme),
      mixer_(theme),
      library_(std::move(librarySource), theme),
      audio_(theme, bus, stats),
      diagnostics_(theme) {
  setupTopBar();
  wireInteractions();

  // Settings Tabs (hidden by default)
  settingsTabs_.setColour(juce::TabbedComponent::backgroundColourId, theme_.background);
  settingsTabs_.setColour(juce::TabbedComponent::outlineColourId, theme_.textDim.withAlpha(0.3f));
  settingsTabs_.addTab("Audio Device", theme_.panel, &audio_, false);
  settingsTabs_.addTab("Hardware Diagnostics", theme_.panel, &diagnostics_, false);
  settingsTabs_.setVisible(false);
  addChildComponent(settingsTabs_);

  addAndMakeVisible(globalWaveform_);
  addAndMakeVisible(deckA_);
  addAndMakeVisible(deckB_);
  addChildComponent(deckC_);
  addChildComponent(deckD_);
  addAndMakeVisible(mixer_);
  addAndMakeVisible(library_);

  setLayoutMode(LayoutMode::TwoDecks);

  setSize(1280, 800);
  startTimerHz(30);  // 30 Hz UI update and telemetry simulation
}

MainComponent::~MainComponent() {
  stopTimer();
}

void MainComponent::setupTopBar() {
  titleLabel_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
  titleLabel_.setColour(juce::Label::textColourId, theme_.accent);
  titleLabel_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(titleLabel_);

  view2DecksBtn_.onClick = [this] { setLayoutMode(LayoutMode::TwoDecks); };
  addAndMakeVisible(view2DecksBtn_);

  view4DecksBtn_.onClick = [this] { setLayoutMode(LayoutMode::FourDecks); };
  addAndMakeVisible(view4DecksBtn_);

  recBtn_.setClickingTogglesState(true);
  recBtn_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  recBtn_.setColour(juce::TextButton::buttonOnColourId, theme_.meterRed.withAlpha(0.3f));
  recBtn_.setColour(juce::TextButton::textColourOffId, theme_.meterRed);
  recBtn_.setColour(juce::TextButton::textColourOnId, theme_.meterRed);
  addAndMakeVisible(recBtn_);

  settingsBtn_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  settingsBtn_.setColour(juce::TextButton::textColourOffId, theme_.text);
  settingsBtn_.onClick = [this] { setSettingsVisible(!showSettings_); };
  addAndMakeVisible(settingsBtn_);

  statsLabel_.setFont(juce::FontOptions(11.0f));
  statsLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  statsLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(statsLabel_);
}

void MainComponent::setLayoutMode(LayoutMode mode) {
  layoutMode_ = mode;
  const bool fourDecks = (mode == LayoutMode::FourDecks);

  view2DecksBtn_.setColour(juce::TextButton::buttonColourId,
                           !fourDecks ? theme_.panel.brighter(0.2f) : theme_.panel);
  view2DecksBtn_.setColour(juce::TextButton::textColourOffId,
                           !fourDecks ? theme_.accent : theme_.textDim);

  view4DecksBtn_.setColour(juce::TextButton::buttonColourId,
                           fourDecks ? theme_.panel.brighter(0.2f) : theme_.panel);
  view4DecksBtn_.setColour(juce::TextButton::textColourOffId,
                           fourDecks ? theme_.accent : theme_.textDim);

  deckC_.setVisible(fourDecks);
  deckD_.setVisible(fourDecks);

  globalWaveform_.setLayoutMode(fourDecks ? GlobalWaveformComponent::LayoutMode::FourDecks
                                          : GlobalWaveformComponent::LayoutMode::TwoDecks);
  mixer_.setLayoutMode(fourDecks ? MixerComponent::LayoutMode::FourChannels
                                 : MixerComponent::LayoutMode::TwoChannels);

  resized();
  repaint();
}

void MainComponent::wireDeck(DeckComponent& deck, core::DeckTelemetry& telem, core::DeckId id) {
  deck.onPlayClicked = [this, &deck, &telem, id](core::DeckId) {
    telem.isPlaying = true;
    deck.updateTelemetry(telem);
    globalWaveform_.updateTelemetry(id, telem);
    (void)bus_.submit(core::Play{id}, origin_);
  };

  deck.onPauseClicked = [this, &deck, &telem, id](core::DeckId) {
    telem.isPlaying = false;
    deck.updateTelemetry(telem);
    globalWaveform_.updateTelemetry(id, telem);
    (void)bus_.submit(core::Pause{id}, origin_);
  };

  deck.onCueClicked = [this, &deck, &telem, id](core::DeckId) {
    telem.isPlaying = false;
    telem.currentTimeSec = 0.0;
    deck.updateTelemetry(telem);
    globalWaveform_.updateTelemetry(id, telem);
    (void)bus_.submit(core::Cue{id}, origin_);
  };

  deck.onSeekRequested = [this, &deck, &telem, id](core::DeckId, double sec) {
    telem.currentTimeSec = sec;
    deck.updateTelemetry(telem);
    globalWaveform_.updateTelemetry(id, telem);
  };

  deck.onPitchChanged = [&telem](core::DeckId, double speed) {
    telem.playbackSpeed = speed;
  };

  deck.onHotCueClicked = [this, &deck, &telem, id](core::DeckId, int cueIdx) {
    if (cueIdx >= 0 && cueIdx < 8) {
      if (!telem.hotCues[static_cast<std::size_t>(cueIdx)].has_value()) {
        core::CuePointTelemetry cue;
        cue.index = cueIdx + 1;
        cue.timeSec = telem.currentTimeSec;
        cue.name = "Cue " + std::to_string(cueIdx + 1);
        cue.color = "#00D2FF";
        telem.hotCues[static_cast<std::size_t>(cueIdx)] = cue;
      } else {
        telem.currentTimeSec = telem.hotCues[static_cast<std::size_t>(cueIdx)]->timeSec;
      }
      deck.updateTelemetry(telem);
      globalWaveform_.updateTelemetry(id, telem);
    }
  };

  deck.onLoopToggleClicked = [this, &deck, &telem, id](core::DeckId) {
    telem.loop.active = !telem.loop.active;
    if (telem.loop.active) {
      telem.loop.startTimeSec = telem.currentTimeSec;
      const double intSec = telem.beatgrid.beatIntervalSec > 0.0 ? telem.beatgrid.beatIntervalSec : 0.5;
      telem.loop.endTimeSec = telem.currentTimeSec + intSec * 4.0;
    }
    deck.updateTelemetry(telem);
    globalWaveform_.updateTelemetry(id, telem);
  };

  deck.onBeatLoopClicked = [this, &deck, &telem, id](core::DeckId, double beats) {
    telem.loop.active = true;
    telem.loop.startTimeSec = telem.currentTimeSec;
    const double intSec = telem.beatgrid.beatIntervalSec > 0.0 ? telem.beatgrid.beatIntervalSec : 0.5;
    telem.loop.endTimeSec = telem.currentTimeSec + intSec * beats;
    deck.updateTelemetry(telem);
    globalWaveform_.updateTelemetry(id, telem);
  };

  deck.onGainChanged = [this, id](core::DeckId, float gainDb) {
    (void)bus_.submit(core::SetGain{id, gainDb}, origin_);
  };

  deck.onStemVolumeChanged = [this, id](core::DeckId, int stemIdx, float vol) {
    if (stemIdx >= 0 && stemIdx < static_cast<int>(core::kStemKindCount)) {
      (void)bus_.submit(core::SetStemVolume{id, static_cast<core::StemKind>(stemIdx), vol}, origin_);
    }
  };

  deck.onStemMuteChanged = [this, id](core::DeckId, int stemIdx, bool muted) {
    if (stemIdx >= 0 && stemIdx < static_cast<int>(core::kStemKindCount)) {
      (void)bus_.submit(core::SetStemMute{id, static_cast<core::StemKind>(stemIdx), muted}, origin_);
    }
  };
}

void MainComponent::wireInteractions() {
  // 1. Library -> Load Track into any of the 4 decks
  library_.onTrackLoadRequested = [this](const core::TrackItem& track, core::DeckId targetDeck) {
    const auto wf = generateDefaultWaveform(track.durationSec, track.bpm);
    auto& target = deck(targetDeck);
    auto& telem = (targetDeck == core::DeckId::A)
                      ? telemetryA_
                      : ((targetDeck == core::DeckId::B)
                             ? telemetryB_
                             : ((targetDeck == core::DeckId::C) ? telemetryC_ : telemetryD_));

    telem.hasTrack = true;
    telem.isPlaying = false;
    telem.currentTimeSec = 0.0;
    telem.durationSec = track.durationSec > 0.0 ? track.durationSec : 180.0;
    telem.beatgrid.bpm = track.bpm > 0.0 ? track.bpm : 174.0;
    telem.beatgrid.beatIntervalSec = 60.0 / telem.beatgrid.bpm;
    telem.beatgrid.firstBeatTimeSec = 0.0;

    target.setTrack(track, wf);
    target.updateTelemetry(telem);
    globalWaveform_.setWaveformData(targetDeck, wf);
    globalWaveform_.updateTelemetry(targetDeck, telem);

    const auto tid = (track.id > 0) ? core::TrackId{track.id} : core::TrackId{1};
    (void)bus_.submit(core::LoadTrack{targetDeck, tid}, origin_);
  };

  // 2. Global Waveform Seeking
  globalWaveform_.onSeekRequested = [this](core::DeckId targetDeck, double sec) {
    auto& telem = (targetDeck == core::DeckId::A)
                      ? telemetryA_
                      : ((targetDeck == core::DeckId::B)
                             ? telemetryB_
                             : ((targetDeck == core::DeckId::C) ? telemetryC_ : telemetryD_));
    telem.currentTimeSec = sec;
    auto& d = deck(targetDeck);
    d.updateTelemetry(telem);
    globalWaveform_.updateTelemetry(targetDeck, telem);
  };

  // 3. Decks A, B, C, D Controls
  wireDeck(deckA_, telemetryA_, core::DeckId::A);
  wireDeck(deckB_, telemetryB_, core::DeckId::B);
  wireDeck(deckC_, telemetryC_, core::DeckId::C);
  wireDeck(deckD_, telemetryD_, core::DeckId::D);

  // 4. Mixer Controls -> CommandBus
  mixer_.onGainChanged = [this](core::DeckId id, float db) {
    (void)bus_.submit(core::SetGain{id, db}, origin_);
  };
  mixer_.onEqChanged = [this](core::DeckId id, core::EqBand band, float db) {
    (void)bus_.submit(core::SetEq{id, band, db}, origin_);
  };
  mixer_.onVolumeChanged = [this](core::DeckId id, float lin) {
    (void)bus_.submit(core::SetVolume{id, lin}, origin_);
  };
  mixer_.onCueChanged = [this](core::DeckId id, bool enabled) {
    (void)bus_.submit(core::SetDeckCue{id, enabled}, origin_);
  };
  mixer_.onCrossfaderChanged = [this](float position) {
    (void)bus_.submit(core::SetCrossfader{position}, origin_);
  };
  mixer_.onCrossfaderCurveChanged = [this](core::CrossfaderCurve curve) {
    (void)bus_.submit(core::SetCrossfaderCurve{curve}, origin_);
  };
  mixer_.onCrossfaderAssignChanged = [this](core::DeckId id, core::CrossfaderAssign assign) {
    (void)bus_.submit(core::SetCrossfaderAssign{id, assign}, origin_);
  };
  mixer_.onMasterGainChanged = [this](float gainDb) {
    (void)bus_.submit(core::SetMasterGain{gainDb}, origin_);
  };
}

DeckComponent& MainComponent::deck(core::DeckId id) {
  switch (id) {
    case core::DeckId::A:
      return deckA_;
    case core::DeckId::B:
      return deckB_;
    case core::DeckId::C:
      return deckC_;
    case core::DeckId::D:
      return deckD_;
  }
  return deckA_;
}

void MainComponent::showHardwareReport(const core::HardwareReport& report) {
  diagnostics_.setReport(report);
  audio_.setDevices(report.audioDevices);
}

void MainComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  globalWaveform_.setTheme(theme);
  deckA_.setTheme(theme);
  deckB_.setTheme(theme);
  deckC_.setTheme(theme);
  deckD_.setTheme(theme);
  mixer_.setTheme(theme);
  library_.setTheme(theme);
  repaint();
}

void MainComponent::setSettingsVisible(bool visible) {
  showSettings_ = visible;
  settingsTabs_.setVisible(visible);
  settingsBtn_.setColour(juce::TextButton::buttonColourId,
                         visible ? theme_.accent.withAlpha(0.3f) : theme_.panel);
  resized();
  repaint();
}

void MainComponent::timerCallback() {
  constexpr double dt = 1.0 / 30.0;

  auto updatePlayback = [dt, this](DeckComponent& d, core::DeckTelemetry& telem, core::DeckId id) {
    if (telem.isPlaying) {
      telem.currentTimeSec += dt * telem.playbackSpeed;
      if (telem.loop.active && telem.currentTimeSec >= telem.loop.endTimeSec) {
        telem.currentTimeSec = telem.loop.startTimeSec;
      }
      if (telem.currentTimeSec >= telem.durationSec) {
        telem.currentTimeSec = telem.durationSec;
        telem.isPlaying = false;
      }
      d.updateTelemetry(telem);
      globalWaveform_.updateTelemetry(id, telem);
    }
  };

  updatePlayback(deckA_, telemetryA_, core::DeckId::A);
  updatePlayback(deckB_, telemetryB_, core::DeckId::B);
  updatePlayback(deckC_, telemetryC_, core::DeckId::C);
  updatePlayback(deckD_, telemetryD_, core::DeckId::D);

  // Animated VU meters
  auto getPeak = [](const core::DeckTelemetry& telem, float phase) {
    return telem.isPlaying ? 0.65f + 0.2f * static_cast<float>(std::sin(telem.currentTimeSec * 10.0 + phase)) : 0.0f;
  };

  const float pA = getPeak(telemetryA_, 0.0f);
  const float pB = getPeak(telemetryB_, 1.0f);
  const float pC = getPeak(telemetryC_, 2.0f);
  const float pD = getPeak(telemetryD_, 3.0f);

  if (layoutMode_ == LayoutMode::FourDecks) {
    std::array<std::pair<float, float>, core::kDeckCount> peaks = {{
        {pA, pA * 0.95f},
        {pB, pB * 0.95f},
        {pC, pC * 0.95f},
        {pD, pD * 0.95f},
    }};
    const float masterPeak = std::max({pA, pB, pC, pD});
    mixer_.updateMeters(peaks, masterPeak, masterPeak * 0.95f);
  } else {
    const float masterPeak = std::max(pA, pB);
    mixer_.updateMeters(pA, pA * 0.95f, pB, pB * 0.95f, masterPeak, masterPeak * 0.95f);
  }

  // Engine stats
  const auto stats = stats_.stats();
  char buf[64];
  std::snprintf(buf, sizeof(buf), "DSP: %.1f%% | SR: %.0f Hz", stats.cpuLoad * 100.0, stats.sampleRate);
  statsLabel_.setText(buf, juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);

  // Top accent line
  g.setColour(theme_.accent);
  g.fillRect(getLocalBounds().removeFromTop(3));
}

void MainComponent::resized() {
  auto area = getLocalBounds().reduced(8);
  area.removeFromTop(3);  // accent line

  // 1. Top Bar (Height 36px)
  auto topBar = area.removeFromTop(36);
  titleLabel_.setBounds(topBar.removeFromLeft(110));

  auto modesArea = topBar.removeFromLeft(200);
  view2DecksBtn_.setBounds(modesArea.removeFromLeft(95).reduced(2, 4));
  view4DecksBtn_.setBounds(modesArea.reduced(2, 4));

  recBtn_.setBounds(topBar.removeFromLeft(75).reduced(2, 4));
  settingsBtn_.setBounds(topBar.removeFromLeft(90).reduced(2, 4));

  statsLabel_.setBounds(topBar.reduced(4, 2));

  area.removeFromTop(6);

  // If Settings Drawer is opened:
  if (showSettings_) {
    settingsTabs_.setBounds(area);
    return;
  }

  // 2. Global Waveform
  const int gwH = (layoutMode_ == LayoutMode::FourDecks) ? 80 : 56;
  globalWaveform_.setBounds(area.removeFromTop(gwH));

  area.removeFromTop(6);

  // 3. Library at bottom (Height ~34% of remaining area, min 160px)
  const int libH = std::clamp(area.getHeight() * 34 / 100, 160, 260);
  library_.setBounds(area.removeFromBottom(libH));

  area.removeFromBottom(6);

  // 4. Middle DJ workstation
  const int totalW = area.getWidth();

  if (layoutMode_ == LayoutMode::FourDecks) {
    // 4 Decks Layout (SPEC §62):
    // Left column: Deck A (top), Deck C (bottom)
    // Center: Mixer (4 channels)
    // Right column: Deck B (top), Deck D (bottom)
    const int mixerW = std::clamp(totalW * 28 / 100, 260, 360);
    const int deckW = (totalW - mixerW - 12) / 2;
    const int totalDeckH = area.getHeight();
    const int deckH = (totalDeckH - 4) / 2;

    auto leftCol = area.removeFromLeft(deckW);
    deckA_.setBounds(leftCol.removeFromTop(deckH));
    leftCol.removeFromTop(4);
    deckC_.setBounds(leftCol);

    area.removeFromLeft(6);
    mixer_.setBounds(area.removeFromLeft(mixerW));
    area.removeFromLeft(6);

    auto rightCol = area;
    deckB_.setBounds(rightCol.removeFromTop(deckH));
    rightCol.removeFromTop(4);
    deckD_.setBounds(rightCol);
  } else {
    // 2 Decks Layout: Deck A (left) | Mixer (center) | Deck B (right)
    const int mixerW = std::clamp(totalW * 22 / 100, 200, 260);
    const int deckW = (totalW - mixerW - 12) / 2;

    deckA_.setBounds(area.removeFromLeft(deckW));
    area.removeFromLeft(6);
    mixer_.setBounds(area.removeFromLeft(mixerW));
    area.removeFromLeft(6);
    deckB_.setBounds(area);
  }
}

}  // namespace zyron::ui
