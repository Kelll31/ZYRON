// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "Core/Audio/DeckTelemetry.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/System/EngineStats.hpp"
#include "Core/System/HardwareInfo.hpp"
#include "UI/AudioSettingsPanel.hpp"
#include "UI/Deck/DeckComponent.hpp"
#include "UI/DiagnosticsPanel.hpp"
#include "UI/Library/LibraryComponent.hpp"
#include "UI/Mixer/MixerComponent.hpp"
#include "UI/Theme.hpp"
#include "UI/Waveform/GlobalWaveformComponent.hpp"

namespace zyron::ui {

/// Comprehensive 2-deck and 4-deck UI shell hosting Decks A/B/C/D, Mixer, Library,
/// Global Waveform, and Settings overlay (SPEC sections 61, 62, 63, ROADMAP P3-12, P4-03).
class MainComponent final : public juce::Component, private juce::Timer {
 public:
  enum class LayoutMode { TwoDecks, FourDecks };

  MainComponent(const Theme& theme, core::CommandBus& bus, const core::AudioEngineStatsSource& stats,
                std::shared_ptr<core::ILibrarySource> librarySource = nullptr);
  ~MainComponent() override;

  void showHardwareReport(const core::HardwareReport& report);
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
  void setupTopBar();
  void wireInteractions();
  void wireDeck(DeckComponent& deck, core::DeckTelemetry& telem, core::DeckId id);

  Theme theme_;
  LayoutMode layoutMode_{LayoutMode::TwoDecks};
  core::CommandBus& bus_;
  const core::AudioEngineStatsSource& stats_;
  core::CommandOrigin origin_{core::CommandOrigin::Kind::Ui, "ui-shell"};

  // Top header bar
  juce::Label titleLabel_{"title", "ZYRON"};
  juce::TextButton view2DecksBtn_{"2 DECKS"};
  juce::TextButton view4DecksBtn_{"4 DECKS"};
  juce::TextButton recBtn_{"● REC"};
  juce::TextButton settingsBtn_{"SETTINGS"};
  juce::Label statsLabel_;

  // Workstation Modules
  GlobalWaveformComponent globalWaveform_;
  DeckComponent deckA_{core::DeckId::A};
  DeckComponent deckB_{core::DeckId::B};
  DeckComponent deckC_{core::DeckId::C};
  DeckComponent deckD_{core::DeckId::D};
  MixerComponent mixer_;
  LibraryComponent library_;

  // Telemetry caches
  core::DeckTelemetry telemetryA_{core::DeckId::A};
  core::DeckTelemetry telemetryB_{core::DeckId::B};
  core::DeckTelemetry telemetryC_{core::DeckId::C};
  core::DeckTelemetry telemetryD_{core::DeckId::D};

  // Settings & Hardware Panels (Drawer / Overlay)
  bool showSettings_{false};
  AudioSettingsPanel audio_;
  DiagnosticsPanel diagnostics_;
  juce::TabbedComponent settingsTabs_{juce::TabbedButtonBar::TabsAtTop};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace zyron::ui
