# SPDX-License-Identifier: AGPL-3.0-only
#
# zyron_ui: the only module allowed to use juce_gui_* (ADR-0003). It sends Commands and reads snapshots; it never
# includes Audio internals.
add_library(zyron_ui STATIC
  AudioSettingsPanel.cpp
  DiagnosticsPanel.cpp
  MainComponent.cpp
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
  FirstRun/FirstRunWizardComponent.cpp)
add_library(zyron::ui ALIAS zyron_ui)

target_include_directories(zyron_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_ui
  PUBLIC  zyron::core zyron::juce_config juce::juce_gui_basics
  PRIVATE juce::juce_recommended_config_flags)
zyron_target_defaults(zyron_ui)
