// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

#include "Core/Commands/CommandBus.hpp"
#include "Core/System/EngineStats.hpp"
#include "Core/System/HardwareInfo.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Audio output selection, the test tone and live engine statistics (ROADMAP P1-05).
///
/// The panel never touches the engine: every change becomes a Command (SET_AUDIO_OUTPUT, SET_TEST_TONE) submitted
/// through the CommandBus, exactly as MIDI or the AI would. Statistics are polled a few times per second through the
/// read-only AudioEngineStatsSource.
class AudioSettingsPanel final : public juce::Component, private juce::Timer {
 public:
  AudioSettingsPanel(const Theme& theme, core::CommandBus& bus, const core::AudioEngineStatsSource& stats);
  ~AudioSettingsPanel() override;

  /// Fills the API and device lists. Only output-capable devices are offered.
  void setDevices(const std::vector<core::AudioDeviceInfo>& devices);

  void resized() override;

 private:
  void timerCallback() override;
  void rebuildDeviceBox(const juce::String& preferredDevice);
  void submitOutput();
  void submitTone();
  void showError(const std::optional<core::CommandError>& error);

  const Theme theme_;
  core::CommandBus& bus_;
  const core::AudioEngineStatsSource& stats_;
  std::vector<core::AudioDeviceInfo> outputs_;

  juce::Label apiLabel_{{}, TRANS("Audio API")};
  juce::Label deviceLabel_{{}, TRANS("Output device")};
  juce::Label rateLabel_{{}, TRANS("Sample rate")};
  juce::Label bufferLabel_{{}, TRANS("Buffer size")};
  juce::ComboBox apiBox_;
  juce::ComboBox deviceBox_;
  juce::ComboBox rateBox_;
  juce::ComboBox bufferBox_;

  juce::ToggleButton toneButton_{TRANS("Test tone (440 Hz)")};
  juce::Slider levelSlider_;
  juce::Label warningLabel_;
  juce::Label statsLabel_;
  juce::Label errorLabel_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsPanel)
};

}  // namespace zyron::ui
