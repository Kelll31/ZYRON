// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/MainComponent.hpp"
#include "UI/Automix/AutomixPersistence.hpp"
#include "UI/Automix/AutomixTaste.hpp"
#include "UI/Localization.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace zyron::ui {

MainComponent::MainComponent(const Theme& theme, core::CommandBus& bus, const core::AudioEngineStatsSource& stats,
                             std::shared_ptr<core::ILibrarySource> librarySource, core::ILiveEngineSource* live,
                             const core::IDeckLoadSource* loads, core::IAutomixControl* automix)
    : theme_(theme),
      bus_(bus),
      stats_(stats),
      live_(live),
      loads_(loads),
      automix_(automix),
      librarySource_(librarySource),
      preferences_(loadPreferences()),
      taste_(loadTaste()),
      globalWaveform_(theme),
      fxHits_(theme),
      deckA_(core::DeckId::A, theme),
      deckB_(core::DeckId::B, theme),
      deckC_(core::DeckId::C, theme),
      deckD_(core::DeckId::D, theme),
      mixer_(theme),
      library_(librarySource, theme),  // a copy: librarySource_ is initialised after library_ (declaration order)
      queue_(theme),
      automixSettings_(theme),
      waveformBar_(&mainLayout_, 1, false, theme),
      mainBar_(&mainLayout_, 3, false, theme),
      deckBarLeft_(&deckLayout_, 1, true, theme),
      deckBarRight_(&deckLayout_, 3, true, theme),
      automixBar_(&automixLayout_, 1, true, theme),
      general_(theme, preferences_),
      audio_(theme, bus, stats),
      diagnostics_(theme) {
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    telemetry_[i].deck = static_cast<core::DeckId>(i);
  }
  setupTopBar();
  wireInteractions();

  // Settings Tabs (hidden by default)
  settingsTabs_.setColour(juce::TabbedComponent::backgroundColourId, theme_.background);
  settingsTabs_.setColour(juce::TabbedComponent::outlineColourId, theme_.textDim.withAlpha(0.3f));
  general_.onLanguageChanged = [this] {
    if (onLanguageChanged) onLanguageChanged();
  };
  general_.onPreferencesChanged = [this](const Preferences& changed) {
    preferences_ = changed;
    savePreferences(changed);
    submit(core::SetMasterProcessing{changed.glue, changed.limiter});
  };
  submit(core::SetMasterProcessing{preferences_.glue, preferences_.limiter});  // the remembered choice, from the start
  settingsTabs_.addTab(TRANS("General"), theme_.panel, &general_, false);
  settingsTabs_.addTab(TRANS("Audio Device"), theme_.panel, &audio_, false);
  settingsTabs_.addTab(TRANS("Hardware Diagnostics"), theme_.panel, &diagnostics_, false);
  settingsTabs_.setVisible(false);
  addChildComponent(settingsTabs_);

  addAndMakeVisible(fxHits_);
  addAndMakeVisible(globalWaveform_);
  addAndMakeVisible(deckA_);
  addAndMakeVisible(deckB_);
  addChildComponent(deckC_);
  addChildComponent(deckD_);
  addAndMakeVisible(mixer_);
  addAndMakeVisible(library_);
  addChildComponent(queue_);
  automixView_.setViewedComponent(&automixSettings_, false);
  automixView_.setScrollBarsShown(true, false);
  addChildComponent(automixView_);
  addChildComponent(automixBar_);
  addAndMakeVisible(mainBar_);
  addAndMakeVisible(waveformBar_);
  addAndMakeVisible(deckBarLeft_);
  addAndMakeVisible(deckBarRight_);

  // Remembered choices: how to mix, and how big the blocks are.
  layoutSizes_ = loadLayoutSizes();
  core::MixProfile rememberedProfile = loadMixProfile();
  rememberedProfile.favourites = favouritesOf(taste_);  // what the DJ keeps choosing by hand weighs in
  automixSettings_.setProfile(rememberedProfile);
  applyMixProfile(automixSettings_.profile());
  automixSettings_.onTasteReset = [this] { resetTaste(); };
  fxHits_.onHit = [this](core::FxHitType type, float level) { triggerFxHit(type, level); };
  automixSettings_.onProfileChanged = [this](const core::MixProfile& profile) {
    saveMixProfile(profile);
    applyMixProfile(profile);
  };
  for (auto* bar : {&mainBar_, &waveformBar_, &deckBarLeft_, &deckBarRight_}) {
    bar->onMoved = [this] { rememberLayout(); };
  }
  automixBar_.onMoved = [this] { rememberLayout(); };
  // Right-click on an edge: that block goes back to its default size (0 = "use the default" in LayoutSizes).
  const auto resetTo = [this](auto clear) {
    return [this, clear] {
      clear(layoutSizes_);
      mainLayoutReady_ = deckLayoutReady_ = automixLayoutReady_ = false;
      resized();
      rememberLayout();
    };
  };
  waveformBar_.onReset = resetTo([](LayoutSizes& s) { s.waveformHeight = 0; });
  mainBar_.onReset = resetTo([](LayoutSizes& s) { s.bottomHeight = 0; });
  const auto resetDecks = resetTo([this](LayoutSizes& s) {
    (layoutMode_ == LayoutMode::FourDecks ? s.mixerWidth4 : s.mixerWidth2) = 0;
    s.deckSplitPercent = 0;
  });
  deckBarLeft_.onReset = resetDecks;
  deckBarRight_.onReset = resetDecks;
  automixBar_.onReset = resetTo([](LayoutSizes& s) { s.automixSettingsWidth = 0; });
  queue_.onTransitionChosen = [this](std::size_t row, std::optional<core::TransitionStyle> style) {
    if (automix_ == nullptr || !automix_->setTransitionStyle(row, style)) {
      showAutomixNotice(TRANS("That transition has already started and cannot be changed"));
      return;
    }
    queue_.update(automix_->queue());
    if (style.has_value()) {
      onTransitionPicked(*style);
    }
  };
  for (auto* tab : {&libraryTabBtn_, &queueTabBtn_}) {
    tab->setClickingTogglesState(false);
    tab->setColour(juce::TextButton::textColourOffId, theme_.textDim);
    addAndMakeVisible(*tab);
  }
  libraryTabBtn_.onClick = [this] { setBottomTab(false); };
  queueTabBtn_.onClick = [this] { setBottomTab(true); };
  queue_.onMoveRequested = [this](std::size_t from, std::size_t to) {
    if (automix_ != nullptr && !automix_->moveUpcoming(from, to)) {
      notice_ = TRANS("That track cannot be moved: it has started or is being prepared").toStdString();
      noticeTicks_ = 0;
    }
  };
  queue_.onJumpToTransitionRequested = [this] {
    if (automix_ == nullptr || !automix_->jumpToTransition()) {
      notice_ = TRANS("Nothing to jump to: start Automix first").toStdString();
      noticeTicks_ = 0;
    }
  };
  library_.onMixNextRequested = [this](const core::TrackItem& track) {
    if (automix_ == nullptr) {
      return;
    }
    const std::string problem = automix_->mixNext(track.id);
    if (!problem.empty()) {
      notice_ = i18n::translateMessage(problem).toStdString();
      noticeTicks_ = 0;
    }
  };
  queue_.onRemoveRequested = [this](std::size_t index) {
    if (automix_ != nullptr && !automix_->removeUpcoming(index)) {
      notice_ = TRANS("That track cannot be removed: it has started or is being prepared").toStdString();
      noticeTicks_ = 0;
    }
  };
  setBottomTab(false);

  setLayoutMode(LayoutMode::TwoDecks);

  setSize(1280, 800);
  startTimerHz(30);  // 30 Hz: read the engine telemetry and repaint
}

MainComponent::~MainComponent() {
  stopTimer();
}

void MainComponent::setupTopBar() {
  addAndMakeVisible(logo_);

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

  automixBtn_.setClickingTogglesState(true);
  automixBtn_.setEnabled(automix_ != nullptr);
  automixBtn_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  automixBtn_.setColour(juce::TextButton::buttonOnColourId, theme_.accent.withAlpha(0.35f));
  automixBtn_.setColour(juce::TextButton::textColourOffId, theme_.text);
  automixBtn_.setColour(juce::TextButton::textColourOnId, theme_.accent);
  automixBtn_.setTooltip(TRANS("Mixes the analysed tracks of the library automatically"));
  automixBtn_.onClick = [this] {
    if (automix_ == nullptr) {
      return;
    }
    if (automixBtn_.getToggleState()) {
      if (automix_->start()) {
        setBottomTab(true);  // show what the AI is about to play
      } else {
        automixBtn_.setToggleState(false, juce::dontSendNotification);
        notice_ = i18n::translateMessage(automix_->telemetry().statusMessage).toStdString();
        noticeTicks_ = 0;
      }
    } else {
      automix_->stop();
    }
  };
  addAndMakeVisible(automixBtn_);

  recBtn_.onClick = [this] { submit(core::SetRecording{recBtn_.getToggleState()}); };
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
  if (automix_ != nullptr) {
    automix_->setVisibleDeckCount(fourDecks ? 4 : 2);  // a live mix never lands on a deck that is switched off
  }

  view2DecksBtn_.setColour(juce::TextButton::buttonColourId,
                           !fourDecks ? theme_.panel.brighter(0.2f) : theme_.panel);
  view2DecksBtn_.setColour(juce::TextButton::textColourOffId,
                           !fourDecks ? theme_.accent : theme_.textDim);

  view4DecksBtn_.setColour(juce::TextButton::buttonColourId,
                           fourDecks ? theme_.panel.brighter(0.2f) : theme_.panel);
  view4DecksBtn_.setColour(juce::TextButton::textColourOffId,
                           fourDecks ? theme_.accent : theme_.textDim);

  mainLayoutReady_ = false;  // each layout has its own default heights and widths
  deckLayoutReady_ = false;
  applyVisibility();
  library_.setFourDecks(fourDecks);

  globalWaveform_.setLayoutMode(fourDecks ? GlobalWaveformComponent::LayoutMode::FourDecks
                                          : GlobalWaveformComponent::LayoutMode::TwoDecks);
  mixer_.setLayoutMode(fourDecks ? MixerComponent::LayoutMode::FourChannels
                                 : MixerComponent::LayoutMode::TwoChannels);

  resized();
  repaint();
}

void MainComponent::submit(const core::Command& command) {
  if (const auto error = bus_.submit(command, origin_)) {
    notice_ = std::string(core::commandName(command)) + ": " + error->message;
  }
}

void MainComponent::requestLoad(const core::TrackItem& track, core::DeckId id) {
  if (track.id <= 0) {
    return;  // only tracks the library knows can be loaded
  }
  const auto i = core::index(id);
  loadedTrack_[i] = track;
  if (librarySource_ != nullptr) {
    if (const auto detail = librarySource_->findTrack(track.id)) {
      loadedTrack_[i] = *detail;  // includes the beat grid anchor, which the list rows do not carry
    }
  }
  telemetry_[i] = core::DeckTelemetry{};
  telemetry_[i].deck = id;
  hasPendingLoopIn_[i] = false;
  refreshMarkers(id);

  deck(id).setTrack(track, {});
  globalWaveform_.setWaveformData(id, {});
  submit(core::LoadTrack{id, core::TrackId{track.id}});
}

void MainComponent::setBeatLoop(core::DeckId id, double beats) {
  const double bpm = loadedTrack_[core::index(id)].bpm;
  if (bpm <= 0.0) {
    notice_ = TRANS("Beat loops need a BPM: this track has not been analysed yet").toStdString();
    return;
  }
  const double start = telemetry(id).currentTimeSec;
  submit(core::SetLoop{id, start, start + beats * 60.0 / bpm, true});
}

void MainComponent::requestScratch(core::DeckId id, core::ScratchPattern pattern, double beats) {
  const auto i = core::index(id);
  const double bpm = loadedTrack_[i].bpm;
  if (bpm <= 0.0) {
    showAutomixNotice(TRANS("Scratches need a BPM: this track has not been analysed yet"));
    return;
  }
  if (!telemetry_[i].isPlaying) {
    showAutomixNotice(TRANS("Scratches need a playing deck"));
    return;
  }
  const double speed = telemetry_[i].playbackSpeed > 0.0 ? telemetry_[i].playbackSpeed : 1.0;
  submit(core::Scratch{id, pattern, beats, 60.0 / (bpm * speed)});
}

void MainComponent::onTransitionPicked(core::TransitionStyle style) {
  recordChoice(taste_, style);
  saveTaste(taste_);
  core::MixProfile profile = automixSettings_.profile();
  const auto favourites = favouritesOf(taste_);
  if (profile.favourites != favourites) {  // a style reached its third pick: the Automix favours it from now on
    profile.favourites = favourites;
    automixSettings_.setProfile(profile);
    applyMixProfile(profile);
  }
}

void MainComponent::resetTaste() {
  taste_.clear();
  saveTaste(taste_);
  core::MixProfile profile = automixSettings_.profile();
  profile.favourites.clear();
  automixSettings_.setProfile(profile);
  applyMixProfile(profile);
}

void MainComponent::showDeckFx(core::DeckId id, juce::Component& target) {
  auto panel = std::make_unique<DeckFxPanel>(id, theme_);
  panel->onSlotChanged = [this](core::DeckId d, int slot, const core::FxSlotState& fx) {
    submit(core::SetFx{d, slot, fx.type, fx.enabled, fx.wet, fx.param, fx.tailAfterFader});
  };
  panel->syncFromState(bus_.state()->deck(id));
  openFxPanel_ = panel.get();
  juce::CallOutBox::launchAsynchronously(std::move(panel), target.getScreenBounds(), nullptr);
}

void MainComponent::triggerFxHit(core::FxHitType type, float level) {
  std::array<DeckBeat, core::kDeckCount> beats{};
  for (std::size_t i = 0; i < beats.size(); ++i) {
    beats[i] = {telemetry_[i].isPlaying, loadedTrack_[i].bpm, telemetry_[i].playbackSpeed};
  }
  const std::size_t visible = layoutMode_ == LayoutMode::FourDecks ? 4 : 2;  // a hidden deck is not the DJ's tempo
  submit(core::TriggerFxHit{type, level, fxHitBeatSeconds(std::span<const DeckBeat>(beats.data(), visible))});
}

void MainComponent::applyLoudnessTrim(core::DeckId id) {
  const auto trim = loudnessTrimDb(loadedTrack_[core::index(id)].loudnessLufs);
  if (preferences_.autoLoudness && trim.has_value()) {
    submit(core::SetTrackGainTrim{id, *trim});
  }
}

void MainComponent::pollFxTempo(core::DeckId id, const core::LiveDeckState& engineDeck) {
  if (!engineDeck.hasTrack) {
    return;
  }
  const auto i = core::index(id);
  if (const auto beat = fxTempo_[i].update(loadedTrack_[i].bpm, engineDeck.playbackSpeed, ticks_)) {
    submit(core::SetFxTempo{id, *beat});
  }
}

void MainComponent::wireDeckSound(DeckComponent& deckView) {
  deckView.onKeylockChanged = [this](core::DeckId d, bool enabled) { submit(core::SetKeylock{d, enabled}); };
  deckView.onKeyShiftChanged = [this](core::DeckId d, float semitones) { submit(core::SetKeyShift{d, semitones}); };
  deckView.onFxButtonClicked = [this](core::DeckId d, juce::Component& target) { showDeckFx(d, target); };
}

void MainComponent::wireDeck(DeckComponent& deckView, core::DeckId id) {
  wireDeckSound(deckView);
  deckView.onPlayClicked = [this](core::DeckId d) { submit(core::Play{d}); };
  deckView.onPauseClicked = [this](core::DeckId d) { submit(core::Pause{d}); };
  deckView.onCueClicked = [this](core::DeckId d) { submit(core::Cue{d}); };
  deckView.onSyncClicked = [this](core::DeckId d) { submit(core::Sync{d}); };
  deckView.onSeparateStemsClicked = [this](core::DeckId d) { submit(core::SeparateStems{d}); };
  deckView.onMarkerAddRequested = [this](core::DeckId d, const std::string& type) { addMarker(d, type); };
  deckView.onMarkerRemoveNearestRequested = [this](core::DeckId d) { removeNearestMarker(d); };
  deckView.onScratchRequested = [this](core::DeckId d, core::ScratchPattern pattern, double beats) {
    requestScratch(d, pattern, beats);
  };
  deckView.onSeekRequested = [this](core::DeckId d, double sec) { submit(core::Seek{d, sec}); };
  deckView.onPitchChanged = [this](core::DeckId d, double speed) { submit(core::SetPlaybackSpeed{d, speed}); };
  deckView.onGainChanged = [this](core::DeckId d, float gainDb) { submit(core::SetGain{d, gainDb}); };

  // Hot cues are view state for now (not persisted): an empty slot stores the real playhead, a set slot jumps to it.
  deckView.onHotCueClicked = [this](core::DeckId d, int cueIdx) {
    if (cueIdx < 0 || cueIdx >= 8) {
      return;
    }
    auto& telem = telemetry(d);
    auto& slot = telem.hotCues[static_cast<std::size_t>(cueIdx)];
    if (!telem.hasTrack) {
      return;
    }
    if (!slot.has_value()) {
      core::CuePointTelemetry cue;
      cue.index = cueIdx + 1;
      cue.timeSec = telem.currentTimeSec;
      cue.name = TRANS("Cue").toStdString() + " " + std::to_string(cueIdx + 1);
      cue.color = "#00D2FF";
      slot = cue;
    } else {
      submit(core::Seek{d, slot->timeSec});
    }
  };

  deckView.onLoopInClicked = [this](core::DeckId d) {
    pendingLoopIn_[core::index(d)] = telemetry(d).currentTimeSec;
    hasPendingLoopIn_[core::index(d)] = true;
  };
  deckView.onLoopOutClicked = [this](core::DeckId d) {
    const auto i = core::index(d);
    const double out = telemetry(d).currentTimeSec;
    if (hasPendingLoopIn_[i] && out > pendingLoopIn_[i]) {
      submit(core::SetLoop{d, pendingLoopIn_[i], out, true});
    }
  };
  deckView.onLoopToggleClicked = [this](core::DeckId d) {
    const auto state = bus_.state();
    const auto& loop = state->deck(d).loop;
    if (loop.endSeconds > loop.startSeconds) {
      submit(core::SetLoop{d, loop.startSeconds, loop.endSeconds, !loop.active});
    }
  };
  deckView.onBeatLoopClicked = [this](core::DeckId d, double beats) { setBeatLoop(d, beats); };

  deckView.onStemVolumeChanged = [this, id](core::DeckId, int stemIdx, float vol) {
    if (stemIdx >= 0 && stemIdx < static_cast<int>(core::kStemKindCount)) {
      submit(core::SetStemVolume{id, static_cast<core::StemKind>(stemIdx), vol});
    }
  };
  deckView.onStemMuteChanged = [this, id](core::DeckId, int stemIdx, bool muted) {
    if (stemIdx >= 0 && stemIdx < static_cast<int>(core::kStemKindCount)) {
      submit(core::SetStemMute{id, static_cast<core::StemKind>(stemIdx), muted});
    }
  };
}

void MainComponent::wireInteractions() {
  // 1. Library -> load a track into any of the 4 decks
  library_.onTrackLoadRequested = [this](const core::TrackItem& track, core::DeckId targetDeck) {
    requestLoad(track, targetDeck);
  };

  // 2. Global waveform seeking
  globalWaveform_.onSeekRequested = [this](core::DeckId targetDeck, double sec) {
    submit(core::Seek{targetDeck, sec});
  };

  // 3. Decks A, B, C, D
  wireDeck(deckA_, core::DeckId::A);
  wireDeck(deckB_, core::DeckId::B);
  wireDeck(deckC_, core::DeckId::C);
  wireDeck(deckD_, core::DeckId::D);

  // 4. Mixer controls -> CommandBus
  mixer_.onGainChanged = [this](core::DeckId id, float db) { submit(core::SetGain{id, db}); };
  mixer_.onFilterChanged = [this](core::DeckId id, float position) { submit(core::SetFilter{id, position}); };
  mixer_.onEqChanged = [this](core::DeckId id, core::EqBand band, float db) { submit(core::SetEq{id, band, db}); };
  mixer_.onVolumeChanged = [this](core::DeckId id, float lin) { submit(core::SetVolume{id, lin}); };
  mixer_.onCueChanged = [this](core::DeckId id, bool enabled) { submit(core::SetDeckCue{id, enabled}); };
  mixer_.onCrossfaderChanged = [this](float position) { submit(core::SetCrossfader{position}); };
  mixer_.onCrossfaderCurveChanged = [this](core::CrossfaderCurve curve) { submit(core::SetCrossfaderCurve{curve}); };
  mixer_.onCrossfaderAssignChanged = [this](core::DeckId id, core::CrossfaderAssign assign) {
    submit(core::SetCrossfaderAssign{id, assign});
  };
  mixer_.onMasterGainChanged = [this](float gainDb) { submit(core::SetMasterGain{gainDb}); };
}

void MainComponent::pollLoadStatus(core::DeckId id) {
  if (loads_ == nullptr) {
    return;
  }
  const auto i = core::index(id);
  const core::DeckLoadStatus status = loads_->loadStatus(id);
  const bool changed = status.generation != shownLoadGeneration_[i];

  // A track can arrive without a click on the library (Automix, MIDI, the AI): the deck header follows the engine.
  // Retried until it succeeds (a busy library database must not leave the header on "No Track Loaded" for good).
  if (status.track.isValid() && status.track.value != loadedTrack_[i].id && librarySource_ != nullptr &&
      (changed || ticks_ % 15 == 0)) {
    try {
      if (const auto track = librarySource_->findTrack(status.track.value)) {
        loadedTrack_[i] = *track;
        telemetry_[i].hotCues = {};
        hasPendingLoopIn_[i] = false;
        deck(id).updateTrackInfo(*track);
        refreshMarkers(id);
      }
    } catch (const std::exception&) {
      // tried again on a later tick
    }
  }
  // A loaded track gets its loudness trim once per load, whoever loaded it (library, Automix, MIDI, the AI). Waits for
  // the library row when it could not be read yet.
  if (status.phase == core::DeckLoadPhase::Ready && status.generation != trimmedGeneration_[i] &&
      status.track.isValid() && loadedTrack_[i].id == status.track.value) {
    trimmedGeneration_[i] = status.generation;
    applyLoudnessTrim(id);
    // Stems ahead of time: the stem controls then work the moment the DJ reaches for them (once per track).
    if (preferences_.autoStems && status.stemPhase == core::StemPhase::None && stemsRequested_[i] != status.track.value) {
      stemsRequested_[i] = status.track.value;
      submit(core::SeparateStems{id});
    }
  }
  if (!changed) {
    return;
  }
  shownLoadGeneration_[i] = status.generation;
  deck(id).setStemStatus(status.stemPhase, status.stemProgress, status.stemMessage);

  switch (status.phase) {
    case core::DeckLoadPhase::Ready:
      if (status.waveform != nullptr) {
        deck(id).setWaveformData(*status.waveform);
        globalWaveform_.setWaveformData(id, *status.waveform);
      }
      break;
    case core::DeckLoadPhase::Failed: {
      core::TrackItem failed;
      failed.title = TRANS("Load failed").toStdString();
      failed.artist = i18n::translateMessage(status.message).toStdString();
      deck(id).setTrack(failed, {});
      globalWaveform_.setWaveformData(id, {});
      break;
    }
    case core::DeckLoadPhase::Loading:
    case core::DeckLoadPhase::Empty:
      break;
  }
}

namespace {

const char* markerLabel(const std::string& type) {
  if (type == "mix_in") return "MIX IN";
  if (type == "mix_out") return "MIX OUT";
  if (type == "drop") return "DROP";
  if (type == "break") return "BREAK";
  if (type == "intro") return "INTRO";
  if (type == "outro") return "OUTRO";
  return "MARK";
}

const char* markerColour(const std::string& type) {
  if (type == "mix_in") return "#00FF88";
  if (type == "mix_out") return "#FFCC00";
  if (type == "drop") return "#FF3366";
  if (type == "break") return "#3399FF";
  if (type == "intro") return "#66D9A8";
  if (type == "outro") return "#FFAA33";
  return "#00D2FF";
}

}  // namespace

void MainComponent::refreshMarkers(core::DeckId id) {
  const auto i = core::index(id);
  telemetry_[i].markers.clear();
  if (librarySource_ == nullptr || loadedTrack_[i].id <= 0) {
    return;
  }
  for (const core::TrackMarker& marker : librarySource_->markers(loadedTrack_[i].id)) {
    core::CuePointTelemetry cue;
    cue.index = marker.id;
    cue.timeSec = marker.timeSec;
    cue.type = marker.type;
    cue.color = markerColour(marker.type);
    cue.name = juce::translate(markerLabel(marker.type)).toStdString() + (marker.source == "auto" ? " (AI)" : "");
    telemetry_[i].markers.push_back(std::move(cue));
  }
}

void MainComponent::addMarker(core::DeckId id, const std::string& type) {
  const auto i = core::index(id);
  const core::TrackItem& track = loadedTrack_[i];
  if (librarySource_ == nullptr || track.id <= 0 || !telemetry_[i].hasTrack) {
    return;
  }
  double position = telemetry_[i].currentTimeSec;
  if (track.bpm > 0.0) {  // marks sit on the beat grid, like the AI's own
    const double period = 60.0 / track.bpm;
    position = track.firstBeatSec + std::round((position - track.firstBeatSec) / period) * period;
    position = std::max(0.0, position);
  }
  core::TrackMarker marker;
  marker.type = type;
  marker.timeSec = position;
  marker.name = markerLabel(type);
  if (librarySource_->setMarker(track.id, marker) == 0) {
    notice_ = TRANS("Could not save the marker").toStdString();
    noticeTicks_ = 0;
    return;
  }
  refreshMarkers(id);
}

void MainComponent::removeNearestMarker(core::DeckId id) {
  const auto i = core::index(id);
  if (librarySource_ == nullptr || loadedTrack_[i].id <= 0 || telemetry_[i].markers.empty()) {
    return;
  }
  const double now = telemetry_[i].currentTimeSec;
  const core::CuePointTelemetry* nearest = &telemetry_[i].markers.front();
  for (const auto& marker : telemetry_[i].markers) {
    if (std::abs(marker.timeSec - now) < std::abs(nearest->timeSec - now)) {
      nearest = &marker;
    }
  }
  librarySource_->removeMarker(loadedTrack_[i].id, nearest->index);
  refreshMarkers(id);
}

void MainComponent::pollAutomix() {
  if (automix_ == nullptr) {
    return;
  }
  if (showQueue_ && ticks_ % 15 == 0) {
    queue_.update(automix_->queue());
  }
  const core::AutonomousDjTelemetry t = automix_->telemetry();
  const bool active =
      t.status == core::AutonomousDjStatus::Running || t.status == core::AutonomousDjStatus::Paused;
  automixBtn_.setToggleState(active, juce::dontSendNotification);

  if (active) {
    automixText_ = TRANS("AUTOMIX").toStdString() + " " + std::to_string(t.currentTrackIndex) + "/" +
                   std::to_string(t.totalTracksInSet) + " | " +
                   i18n::translateMessage(t.statusMessage).toStdString();
  } else {
    automixText_.clear();
  }
  if (t.status == core::AutonomousDjStatus::Error && lastAutomixStatus_ != core::AutonomousDjStatus::Error) {
    notice_ = i18n::translateMessage(t.statusMessage).toStdString();  // e.g. a track failed to load
    noticeTicks_ = 0;
  }
  lastAutomixStatus_ = t.status;
}

void MainComponent::applyMixProfile(const core::MixProfile& profile) {
  if (automix_ != nullptr) {
    automix_->setMixProfile(profile);
  }
}

void MainComponent::showAutomixNotice(const juce::String& text) {
  notice_ = text.toStdString();
  noticeTicks_ = 0;
}

void MainComponent::pollTransitions() {
  // Where the next mix loops, scratches and drops, on each track: a few times a second is plenty for a planned thing.
  if (automix_ == nullptr || ticks_ % 8 != 0) {
    return;
  }
  const core::TransitionPreview preview = automix_->transitionPreview();
  std::array<std::vector<core::TransitionRegion>, core::kDeckCount> perDeck;
  for (const core::TransitionRegion& region : preview.regions) {
    perDeck[core::index(region.deck)].push_back(region);
  }
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    const auto id = static_cast<core::DeckId>(i);
    deck(id).setTransitionRegions(perDeck[i]);
    globalWaveform_.setTransitionRegions(id, perDeck[i]);
  }
}

void MainComponent::pollLibraryStatus() {
  library_.updateScanStatus();
}

void MainComponent::timerCallback() {
  const auto state = bus_.state();
  core::LiveEngineState live;
  if (live_ != nullptr) {
    live = live_->liveState();
  }

  pollAutomix();
  pollTransitions();
  recBtn_.setToggleState(state->recording, juce::dontSendNotification);  // the state, not the click, is the truth

  std::array<std::pair<float, float>, core::kDeckCount> peaks{};
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    const auto id = static_cast<core::DeckId>(i);
    pollLoadStatus(id);

    const core::LiveDeckState& engineDeck = live.decks[i];
    const core::DeckState& intent = state->decks[i];
    core::DeckTelemetry& telem = telemetry_[i];
    telem.hasTrack = engineDeck.hasTrack;
    telem.isPlaying = engineDeck.isPlaying;
    telem.currentTimeSec = engineDeck.positionSec;
    telem.durationSec = engineDeck.durationSec;
    telem.playbackSpeed = engineDeck.playbackSpeed;  // the real speed: a sync moves it behind the user's back
    telem.vuLevelLeft = engineDeck.peakLeft;
    telem.vuLevelRight = engineDeck.peakRight;
    telem.hasStems = engineDeck.hasStems;
    telem.loop.active = intent.loop.active;
    telem.loop.startTimeSec = intent.loop.startSeconds;
    telem.loop.endTimeSec = intent.loop.endSeconds;
    const core::TrackItem& row = loadedTrack_[i];
    telem.beatgrid.bpm = row.bpm;  // from analysis; 0 until the track has been analysed
    telem.beatgrid.firstBeatTimeSec = row.firstBeatSec;
    telem.beatgrid.beatIntervalSec = row.bpm > 0.0 ? 60.0 / row.bpm : 0.0;

    pollFxTempo(id, engineDeck);
    deck(id).updateTelemetry(telem);
    deck(id).syncFromState(intent);
    if (openFxPanel_ != nullptr && openFxPanel_->deckId() == id) {
      openFxPanel_->syncFromState(intent);
    }
    globalWaveform_.updateTelemetry(id, telem);
    peaks[i] = {engineDeck.peakLeft, engineDeck.peakRight};
  }

  mixer_.syncFromState(*state);  // Automix, MIDI and the AI move the same Commands: show them

  if (layoutMode_ == LayoutMode::FourDecks) {
    mixer_.updateMeters(peaks, live.masterPeakLeft, live.masterPeakRight);
  } else {
    mixer_.updateMeters(peaks[0].first, peaks[0].second, peaks[1].first, peaks[1].second, live.masterPeakLeft,
                        live.masterPeakRight);
  }

  // Library: scan progress and new tracks, twice a second; pick up tempo and key as analysis finishes.
  if (ticks_ % 60 == 30) {  // the AI's marker proposals arrive while the analysis runs
    for (std::size_t i = 0; i < core::kDeckCount; ++i) {
      if (loadedTrack_[i].id > 0) {
        refreshMarkers(static_cast<core::DeckId>(i));
      }
    }
  }
  if (++ticks_ % 15 == 0) {
    pollLibraryStatus();
    for (std::size_t i = 0; i < core::kDeckCount; ++i) {
      if (librarySource_ != nullptr && loadedTrack_[i].id > 0 && loadedTrack_[i].bpm <= 0.0) {
        if (const auto fresh = librarySource_->findTrack(loadedTrack_[i].id); fresh && fresh->bpm > 0.0) {
          loadedTrack_[i] = *fresh;
          deck(static_cast<core::DeckId>(i)).updateTrackInfo(*fresh);
        }
      }
    }
  }
  if (live.noticeSerial != shownNoticeSerial_) {
    shownNoticeSerial_ = live.noticeSerial;
    notice_ = i18n::translateMessage(live.notice).toStdString();
    noticeTicks_ = 0;
  }

  // Status line: a rejected command wins for a couple of seconds, then the engine figures.
  const auto engineStats = stats_.stats();
  if (!notice_.empty()) {
    statsLabel_.setText(notice_, juce::dontSendNotification);
    if (++noticeTicks_ > 150) {  // about five seconds
      notice_.clear();
    }
  } else if (automix_ != nullptr && !automix_->liveMixStatus().empty()) {
    statsLabel_.setText(i18n::translateMessage(automix_->liveMixStatus()), juce::dontSendNotification);
  } else if (!automixText_.empty()) {
    statsLabel_.setText(automixText_, juce::dontSendNotification);
  } else {
    statsLabel_.setText(juce::String(TRANS("DSP: %cpu% | SR: %sr% Hz | xruns: %xr% | dropped: %dr%"))
                            .replace("%cpu%", juce::String(engineStats.cpuLoad * 100.0, 1) + "%")
                            .replace("%sr%", juce::String(engineStats.sampleRate, 0))
                            .replace("%xr%", juce::String(engineStats.xrunCount))
                            .replace("%dr%", juce::String(static_cast<juce::int64>(live.droppedMessages))),
                        juce::dontSendNotification);
  }
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
  fxHits_.setTheme(theme);
  deckA_.setTheme(theme);
  deckB_.setTheme(theme);
  deckC_.setTheme(theme);
  deckD_.setTheme(theme);
  mixer_.setTheme(theme);
  library_.setTheme(theme);
  queue_.setTheme(theme);
  automixSettings_.setTheme(theme);
  mainBar_.setTheme(theme);
  waveformBar_.setTheme(theme);
  deckBarLeft_.setTheme(theme);
  deckBarRight_.setTheme(theme);
  automixBar_.setTheme(theme);
  repaint();
}

void MainComponent::applyVisibility() {
  // The settings panel replaces the workstation: the decks must not stay visible (and clickable) behind it.
  const bool workstation = !showSettings_;
  const bool fourDecks = layoutMode_ == LayoutMode::FourDecks;
  globalWaveform_.setVisible(workstation);
  fxHits_.setVisible(workstation);
  deckA_.setVisible(workstation);
  deckB_.setVisible(workstation);
  deckC_.setVisible(workstation && fourDecks);
  deckD_.setVisible(workstation && fourDecks);
  mixer_.setVisible(workstation);
  library_.setVisible(workstation && !showQueue_);
  queue_.setVisible(workstation && showQueue_);
  automixView_.setVisible(workstation && showQueue_);
  automixBar_.setVisible(workstation && showQueue_);
  mainBar_.setVisible(workstation);
  waveformBar_.setVisible(workstation);
  deckBarLeft_.setVisible(workstation);
  deckBarRight_.setVisible(workstation);
  libraryTabBtn_.setVisible(workstation);
  queueTabBtn_.setVisible(workstation);
  settingsTabs_.setVisible(showSettings_);
  if (showSettings_) {
    settingsTabs_.toFront(false);
  }
}

void MainComponent::setBottomTab(bool showQueue) {
  showQueue_ = showQueue;
  libraryTabBtn_.setColour(juce::TextButton::buttonColourId, showQueue ? theme_.panel : theme_.panel.brighter(0.25f));
  queueTabBtn_.setColour(juce::TextButton::buttonColourId, showQueue ? theme_.panel.brighter(0.25f) : theme_.panel);
  libraryTabBtn_.setColour(juce::TextButton::textColourOffId, showQueue ? theme_.textDim : theme_.accent);
  queueTabBtn_.setColour(juce::TextButton::textColourOffId, showQueue ? theme_.accent : theme_.textDim);
  if (showQueue && automix_ != nullptr) {
    queue_.update(automix_->queue());
  }
  applyVisibility();
  resized();
}

void MainComponent::setSettingsVisible(bool visible) {
  showSettings_ = visible;
  applyVisibility();
  settingsBtn_.setColour(juce::TextButton::buttonColourId,
                         visible ? theme_.accent.withAlpha(0.3f) : theme_.panel);
  resized();
  repaint();
}

}  // namespace zyron::ui
