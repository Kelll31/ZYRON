# SPDX-License-Identifier: AGPL-3.0-only
#
# zyron_midi: MIDI I/O, mapping and MIDI Learn (ROADMAP Phase 9). It speaks to the rest of the app only through the
# Command API. For now this holds the device probe.
add_library(zyron_midi STATIC
  JuceMidiProbe.cpp)
add_library(zyron::midi ALIAS zyron_midi)

target_include_directories(zyron_midi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
# The public header (JuceMidiProbe.hpp) is JUCE-free, so JUCE stays an implementation detail of this library.
target_link_libraries(zyron_midi
  PUBLIC  zyron::core
  PRIVATE zyron::juce_config juce::juce_audio_devices juce::juce_recommended_config_flags)
zyron_target_defaults(zyron_midi)
