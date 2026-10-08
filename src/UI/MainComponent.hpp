// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include <array>
#include <cstdint>
#include <string>

#include "Core/AI/AutonomousDjTypes.hpp"
#include "Core/Audio/DeckTelemetry.hpp"
#include "Core/Audio/EngineView.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/System/EngineStats.hpp"
#include "Core/System/HardwareInfo.hpp"
#include "UI/AudioSettingsPanel.hpp"
#include "UI/GeneralSettingsPanel.hpp"
#include "UI/Automix/AutomixPersistence.hpp"
#include "UI/Automix/AutomixQueueComponent.hpp"
#include "UI/Automix/AutomixSettingsPanel.hpp"
#include "UI/Automix/AutomixTaste.hpp"
#include "UI/Deck/DeckComponent.hpp"
#include "UI/DiagnosticsPanel.hpp"
#include "UI/Fx/DeckFxPanel.hpp"
#include "UI/Fx/FxHitsBar.hpp"
#include "UI/Fx/FxModel.hpp"
#include "UI/LogoComponent.hpp"
#include "UI/Library/LibraryComponent.hpp"
#include "UI/Mixer/MixerComponent.hpp"
#include "UI/Settings/Preferences.hpp"
#include "UI/SplitterBar.hpp"
#include "UI/Theme.hpp"
#include "UI/Waveform/GlobalWaveformComponent.hpp"

namespace zyron::ui {

/// Comprehensive 2-deck and 4-deck UI shell hosting Decks A/B/C/D, Mixer, Library,
/// Global Waveform, and Settings overlay (SPEC sections 61, 62, 63, ROADMAP P3-12, P4-03).
class MainComponent final : public juce::Component, private juce::Timer {
 public:
  enum class LayoutMode { TwoDecks, FourDecks };

  /// `live` and `loads` are the engine's read-only views (telemetry and load status). Without them the decks simply
  /// stay idle: the UI never makes up playback state.
  MainComponent(const Theme& theme, core::CommandBus& bus, const core::AudioEngineStatsSource& stats,
                std::shared_ptr<core::ILibrarySource> librarySource = nullptr,
                core::ILiveEngineSource* live = nullptr, const core::IDeckLoadSource* loads = nullptr,
                core::IAutomixControl* automix = nullptr);
  ~MainComponent() override;

  void showHardwareReport(const core::HardwareReport& report);

  /// The user picked another language in Settings (already applied and saved): the owner rebuilds the window.
  std::function<void()> onLanguageChanged;
  void setTheme(const Theme& theme);

  void setLayoutMode(LayoutMode mode);
  [[nodiscard]] LayoutMode layoutMode() const noexcept { return layoutMode_; }

  // Subcomponent accessors
  [[nodiscard]] DeckComponent& deck(core::DeckId id);
  [[nodiscard]] MixerComponent& mixer() noexcept { return mixer_; }
  [[nodiscard]] LibraryComponent& library() noexcept { return library_; }
  [[nodiscard]] GlobalWaveformComponent& globalWaveform() noexcept { return globalWaveform_; }

  void setSettingsVisible(bool visible);
  [[nodiscard]] bool isSettingsVisible() const noexcept { return showSettings_; }

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  void timerCallback() override;
  void applyVisibility();
  void setBottomTab(bool showQueue);
  void setupTopBar();
  void wireInteractions();
  void wireDeck(DeckComponent& deck, core::DeckId id);
  void requestLoad(const core::TrackItem& track, core::DeckId id);
  void pollLoadStatus(core::DeckId id);
  void pollLibraryStatus();
  void pollAutomix();
  void pollTransitions();
  void applyMixProfile(const core::MixProfile& profile);
  void showAutomixNotice(const juce::String& text);
  void configureMainLayout(int totalHeight);
  void configureDeckLayout(int totalWidth);
  void layOutDecks(juce::Rectangle<int> area);
  void requestScratch(core::DeckId id, core::ScratchPattern pattern, double beats);
  void configureAutomixLayout(int totalWidth);
  void rememberLayout();
  void refreshMarkers(core::DeckId id);
  void addMarker(core::DeckId id, const std::string& type);
  void removeNearestMarker(core::DeckId id);
  void submit(const core::Command& command);
  [[nodiscard]] core::DeckTelemetry& telemetry(core::DeckId id) { return telemetry_[core::index(id)]; }
  void setBeatLoop(core::DeckId id, double beats);
  void wireDeckSound(DeckComponent& deck);
  void showDeckFx(core::DeckId id, juce::Component& target);
  void triggerFxHit(core::FxHitType type, float level);
  void applyLoudnessTrim(core::DeckId id);
  void pollFxTempo(core::DeckId id, const core::LiveDeckState& engineDeck);
  void onTransitionPicked(core::TransitionStyle style);
  void resetTaste();

  Theme theme_;
  LayoutMode layoutMode_{LayoutMode::TwoDecks};
  core::CommandBus& bus_;
  const core::AudioEngineStatsSource& stats_;
  core::CommandOrigin origin_{core::CommandOrigin::Kind::Ui, "ui-shell"};
  // Engine views, declared before the components: the constructor's initialiser list fills them first, and a component
  // built from one of the same arguments must not see a moved-from value.
  core::ILiveEngineSource* live_{nullptr};
  const core::IDeckLoadSource* loads_{nullptr};
  core::IAutomixControl* automix_{nullptr};
  std::shared_ptr<core::ILibrarySource> librarySource_;
  Preferences preferences_;  // general settings (auto loudness, master glue / limiter), loaded before general_
  TasteCounts taste_;        // transitions the DJ picked by hand, counted

  // Top header bar
  LogoComponent logo_;
  juce::TextButton view2DecksBtn_{TRANS("2 DECKS")};
  juce::TextButton view4DecksBtn_{TRANS("4 DECKS")};
  juce::TextButton recBtn_{"REC"};
  juce::TextButton automixBtn_{TRANS("AUTOMIX")};
  juce::TextButton settingsBtn_{TRANS("SETTINGS")};
  juce::Label statsLabel_;

  // Workstation Modules
  GlobalWaveformComponent globalWaveform_;
  FxHitsBar fxHits_;
  DeckComponent deckA_{core::DeckId::A};
  DeckComponent deckB_{core::DeckId::B};
  DeckComponent deckC_{core::DeckId::C};
  DeckComponent deckD_{core::DeckId::D};
  MixerComponent mixer_;
  LibraryComponent library_;
  AutomixQueueComponent queue_;
  AutomixSettingsPanel automixSettings_;
  juce::Viewport automixView_;  // scrolls the settings panel when the bottom area is too short for all its controls
  juce::TextButton libraryTabBtn_{TRANS("LIBRARY")};
  juce::TextButton queueTabBtn_{TRANS("AUTOMIX")};
  bool showQueue_{false};

  // Resizable blocks, top to bottom: global waveform | bar | decks+mixer | bar | library-or-automix. Across the middle:
  // left decks | bar | mixer | bar | right decks. Inside Automix: queue | bar | settings. The slots stand in for blocks
  // that are laid out by hand inside the rectangles they get.
  juce::StretchableLayoutManager mainLayout_;
  juce::StretchableLayoutManager deckLayout_;
  juce::StretchableLayoutManager automixLayout_;
  SplitterBar waveformBar_;
  SplitterBar mainBar_;
  SplitterBar deckBarLeft_;
  SplitterBar deckBarRight_;
  SplitterBar automixBar_;
  juce::Component waveformSlot_;
  juce::Component middleSlot_;
  juce::Component bottomSlot_;
  juce::Component leftDecksSlot_;
  juce::Component rightDecksSlot_;
  bool deckLayoutReady_{false};
  LayoutSizes layoutSizes_;
  bool mainLayoutReady_{false};
  bool automixLayoutReady_{false};

  // Engine views and per-deck view state. Everything playback-related is read from the engine each tick; the only
  // things kept here are what the engine does not know (the library row, hot cues, a pending loop-in point).
  std::array<core::DeckTelemetry, core::kDeckCount> telemetry_{};
  std::array<core::TrackItem, core::kDeckCount> loadedTrack_{};
  std::array<FxTempoTracker, core::kDeckCount> fxTempo_{};
  std::array<std::uint64_t, core::kDeckCount> trimmedGeneration_{};  // load generation whose loudness trim was sent
  juce::Component::SafePointer<DeckFxPanel> openFxPanel_;
  std::array<std::uint64_t, core::kDeckCount> shownLoadGeneration_{};
  std::array<std::int64_t, core::kDeckCount> stemsRequested_{};  // the track whose stems were asked for, per deck
  std::array<double, core::kDeckCount> pendingLoopIn_{};
  std::array<bool, core::kDeckCount> hasPendingLoopIn_{};
  int ticks_{0};
  std::uint64_t shownNoticeSerial_{0};
  int noticeTicks_{0};
  std::string automixText_;
  core::AutonomousDjStatus lastAutomixStatus_{core::AutonomousDjStatus::Stopped};
  std::string notice_;  // last command rejection, shown in the status label until the next tick of normal stats

  // Settings & Hardware Panels (Drawer / Overlay)
  bool showSettings_{false};
  GeneralSettingsPanel general_;
  AudioSettingsPanel audio_;
  DiagnosticsPanel diagnostics_;
  juce::TabbedComponent settingsTabs_{juce::TabbedButtonBar::TabsAtTop};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace zyron::ui
