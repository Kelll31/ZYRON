# SPDX-License-Identifier: AGPL-3.0-only
#
# Audio: the realtime engine side.
#   zyron_dsp   - JUCE-free DSP building blocks (testable offline). Always built.
#   zyron_audio - device I/O and the engine callback. juce_audio_devices may be used only here (Routing/Engine) and in
#                 MIDI (ADR-0003). Built with the app.
zyron_require_signalsmith_stretch()

add_library(zyron_dsp STATIC
  Bridge/CommandBridge.cpp
  DSP/ChannelStrip.cpp
  DSP/DjFilter.cpp
  DSP/FxHitPlayer.cpp
  DSP/MasterLimiter.cpp
  DSP/Mixer.cpp
  DSP/OutputGate.cpp
  DSP/OutputStage.cpp
  DSP/StemMixer.cpp
  DSP/TimeStretcher.cpp
  DSP/KeylockRenderer.cpp
  DSP/GlueCompressor.cpp
  DSP/TestToneGenerator.cpp
  DSP/ThreeBandEq.cpp
  Deck/CueLoopManager.cpp
  Deck/DeckPlayer.cpp
  Deck/SyncManager.cpp
  Deck/TrackLoader.cpp
  Decoder/WavDecoder.cpp
  Effects/DelayEffect.cpp
  Effects/EchoEffect.cpp
  Effects/EffectRegistry.cpp
  Effects/EffectSlot.cpp
  Effects/FlangerEffect.cpp
  Effects/FxUnit.cpp
  Effects/PhaserEffect.cpp
  Effects/ReverbEffect.cpp
  Engine/AudioGraph.cpp
  Routing/CueRouter.cpp
  Routing/StemRouter.cpp)
add_library(zyron::dsp ALIAS zyron_dsp)
target_include_directories(zyron_dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_dsp PRIVATE zyron::signalsmith_stretch)
zyron_target_defaults(zyron_dsp)

if(ZYRON_BUILD_APP)
  add_library(zyron_audio STATIC
    Decoder/JuceAudioFileDecoder.cpp
    Engine/AudioEngine.cpp
    Routing/JuceAudioDeviceProbe.cpp)
  add_library(zyron::audio ALIAS zyron_audio)

  target_include_directories(zyron_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_link_libraries(zyron_audio
    PUBLIC  zyron::core zyron::dsp zyron::juce_config juce::juce_audio_devices juce::juce_audio_formats
    PRIVATE juce::juce_recommended_config_flags)
  zyron_target_defaults(zyron_audio)
endif()
