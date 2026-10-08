# SPDX-License-Identifier: AGPL-3.0-only
#
# zyron_ui: the only module allowed to use juce_gui_* (ADR-0003). It sends Commands and reads snapshots; it never
# includes Audio internals.
add_library(zyron_ui STATIC
  AudioSettingsPanel.cpp
  Localization.cpp
  LocalizationRu.cpp
  DiagnosticsPanel.cpp
  MainComponent.cpp
  MainComponentLayout.cpp
  MainWindow.cpp
  Waveform/WaveformView.cpp
  Waveform/GlobalWaveformComponent.cpp
  Deck/DeckComponent.cpp
  Mixer/MixerComponent.cpp
  Library/LibraryComponent.cpp
  Settings/ModelManagerComponent.cpp
  Settings/BackgroundQueueComponent.cpp
  AI/RecommendationPanel.cpp
  AI/SetBuilderComponent.cpp
  Automix/AutomixQueueComponent.cpp
  Automix/AutomixSettingsPanel.cpp
  Automix/AutomixPersistence.cpp
  Automix/TransitionLabels.cpp
  Automix/AutomixTaste.cpp
  Fx/FxModel.cpp
  Fx/FxHitsBar.cpp
  Fx/DeckFxPanel.cpp
  Settings/Preferences.cpp
  FirstRun/FirstRunWizardComponent.cpp)
add_library(zyron::ui ALIAS zyron_ui)

# The logo and the application icon are embedded in the executable (resources/branding).
juce_add_binary_data(zyron_branding
  HEADER_NAME "BrandingData.h"
  NAMESPACE zyron_branding
  SOURCES
    "${CMAKE_SOURCE_DIR}/resources/branding/zyron-mark.png"
    "${CMAKE_SOURCE_DIR}/resources/branding/zyron-wordmark.png"
    "${CMAKE_SOURCE_DIR}/resources/branding/zyron-logo.png"
    "${CMAKE_SOURCE_DIR}/resources/branding/zyron-icon.png")
set_target_properties(zyron_branding PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_include_directories(zyron_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_ui
  PUBLIC  zyron::core zyron::juce_config juce::juce_gui_basics zyron_branding  # LogoComponent.hpp includes the data
  PRIVATE juce::juce_recommended_config_flags)
zyron_target_defaults(zyron_ui)
