// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <utility>

#include "Core/Audio/MixerTypes.hpp"
#include "Core/Commands/CommandTypes.hpp"
#include "Core/State/Ids.hpp"
#include "UI/ParamSlider.hpp"
#include "Core/State/AppState.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Dual-channel stereo peak level meter widget with green/yellow/red LED ladder styling.
class LevelMeter : public juce::Component {
 public:
  explicit LevelMeter(Theme theme = Theme::dark());
  void setTheme(const Theme& theme);
  void setLevels(float peakLeft, float peakRight);

  void paint(juce::Graphics& g) override;

 private:
  Theme theme_;
  float peakLeft_{0.0f};
  float peakRight_{0.0f};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

/// 2-to-4 Channel DJ Mixer component with Gain, 3-Band EQ, Filter, Volume Faders, Cue, Master,
/// Crossfader assign selectors, and Crossfader curve control (SPEC sections 20, 61, 62).
class MixerComponent : public juce::Component {
 public:
  enum class LayoutMode { TwoChannels, FourChannels };

  explicit MixerComponent(Theme theme = Theme::dark());
  ~MixerComponent() override = default;

  void setTheme(const Theme& theme);
  void setLayoutMode(LayoutMode mode);
  [[nodiscard]] LayoutMode layoutMode() const noexcept { return layoutMode_; }

  // Telemetry meter updates
  /// Moves the knobs and faders to the state the engine was given (by the user, Automix, MIDI or the AI), so what is
  /// on screen is what is heard. Controls the user is holding are left alone.
  void syncFromState(const core::AppState& state);

  void updateMeters(float chAPeakL, float chAPeakR, float chBPeakL, float chBPeakR,
                    float masterPeakL, float masterPeakR);
  void updateMeters(const std::array<std::pair<float, float>, core::kDeckCount>& channelPeaks,
                    float masterPeakL, float masterPeakR);

  // User Action Callbacks
  std::function<void(core::DeckId deck, float gainDb)> onGainChanged;
  std::function<void(core::DeckId deck, core::EqBand band, float gainDb)> onEqChanged;
  std::function<void(core::DeckId deck, float bipolar)> onFilterChanged;
  std::function<void(core::DeckId deck, float volumeLinear)> onVolumeChanged;
  std::function<void(core::DeckId deck, bool enabled)> onCueChanged;
  std::function<void(core::DeckId deck, core::CrossfaderAssign assign)> onCrossfaderAssignChanged;
  std::function<void(float position)> onCrossfaderChanged;
  std::function<void(core::CrossfaderCurve curve)> onCrossfaderCurveChanged;
  std::function<void(float masterGainDb)> onMasterGainChanged;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  struct ChannelControls {
    juce::Label label;
    ParamSlider gainKnob;
    ParamSlider highKnob;
    ParamSlider midKnob;
    ParamSlider lowKnob;
    ParamSlider filterKnob;
    juce::TextButton cueButton{"CUE"};
    LevelMeter meter;
    ParamSlider volumeFader;
    juce::ComboBox assignSelector;
  };

  void setupChannel(ChannelControls& ch, core::DeckId deck, const juce::String& name, juce::Colour accent);
  void setupMasterSection();
  void setupCrossfaderSection();

  Theme theme_;
  LayoutMode layoutMode_{LayoutMode::TwoChannels};

  std::array<ChannelControls, core::kDeckCount> channels_;

  // Master Section
  juce::Label masterLabel_{"master", "MASTER"};
  ParamSlider masterGainKnob_;
  LevelMeter masterMeter_;

  // Crossfader Section
  ParamSlider crossfaderSlider_;
  juce::ComboBox curveSelector_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerComponent)
};

}  // namespace zyron::ui
