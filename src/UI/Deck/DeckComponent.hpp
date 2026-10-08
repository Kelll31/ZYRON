// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <string>

#include "Core/Audio/DeckTelemetry.hpp"
#include "Core/Audio/WaveformData.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/Ids.hpp"
#include "UI/Theme.hpp"
#include "UI/Waveform/WaveformView.hpp"

namespace zyron::ui {

/// Full-featured DJ deck UI component (SPEC sections 12, 14, 25, 26, 61).
/// Integrates track display, detail waveform, 4-stem strip, 8 hot cues, loop controls, transport and pitch fader.
class DeckComponent : public juce::Component {
 public:
  DeckComponent(core::DeckId deckId, Theme theme = Theme::dark());
  ~DeckComponent() override = default;

  [[nodiscard]] core::DeckId deckId() const noexcept { return deckId_; }

  void setTheme(const Theme& theme);
  void setTrack(const core::TrackItem& track, core::WaveformData waveform = {});
  void setWaveformData(core::WaveformData waveform);
  void updateTelemetry(const core::DeckTelemetry& telemetry);

  // User Action Callbacks
  std::function<void(core::DeckId deck)> onPlayClicked;
  std::function<void(core::DeckId deck)> onPauseClicked;
  std::function<void(core::DeckId deck)> onCueClicked;
  std::function<void(core::DeckId deck)> onSyncClicked;
  std::function<void(core::DeckId deck, double seekSec)> onSeekRequested;
  std::function<void(core::DeckId deck, int cueIndex)> onHotCueClicked;
  std::function<void(core::DeckId deck)> onLoopInClicked;
  std::function<void(core::DeckId deck)> onLoopOutClicked;
  std::function<void(core::DeckId deck)> onLoopToggleClicked;
  std::function<void(core::DeckId deck, double beats)> onBeatLoopClicked;
  std::function<void(core::DeckId deck, double speedRatio)> onPitchChanged;
  std::function<void(core::DeckId deck, float gainDb)> onGainChanged;
  std::function<void(core::DeckId deck, int stemIndex, float volume)> onStemVolumeChanged;
  std::function<void(core::DeckId deck, int stemIndex, bool muted)> onStemMuteChanged;

  void paint(juce::Graphics& g) override;
  void resized() override;

  [[nodiscard]] WaveformView& waveformView() noexcept { return waveformView_; }

 private:
  void setupHeader();
  void setupTransport();
  void setupLoops();
  void setupHotCues();
  void setupStems();
  void setupPitchFader();
  void updateTimeLabels(double currentSec, double durationSec);

  core::DeckId deckId_;
  Theme theme_;
  core::TrackItem track_;
  core::DeckTelemetry telemetry_;

  // Header UI
  juce::Label deckBadgeLabel_;
  juce::Label titleLabel_;
  juce::Label artistLabel_;
  juce::Label bpmLabel_;
  juce::Label keyLabel_;
  juce::Label timeElapsedLabel_;
  juce::Label timeRemainingLabel_;

  // Waveform
  WaveformView waveformView_;

  // Stems strip (VOC, DRUM, BASS, OTHER - SPEC §61)
  static constexpr int kNumStems = 4;
  std::array<juce::TextButton, kNumStems> stemMuteButtons_{
      juce::TextButton{"VOC"}, juce::TextButton{"DRUM"}, juce::TextButton{"BASS"}, juce::TextButton{"OTHER"}};
  std::array<juce::Slider, kNumStems> stemVolumeSliders_{};

  // Hot Cues 1..8 (SPEC §25)
  static constexpr int kNumCues = 8;
  std::array<juce::TextButton, kNumCues> hotCueButtons_{
      juce::TextButton{"1"}, juce::TextButton{"2"}, juce::TextButton{"3"}, juce::TextButton{"4"},
      juce::TextButton{"5"}, juce::TextButton{"6"}, juce::TextButton{"7"}, juce::TextButton{"8"}};

  // Loop Controls (SPEC §26)
  juce::TextButton loopInButton_{"IN"};
  juce::TextButton loopOutButton_{"OUT"};
  juce::TextButton loopActiveButton_{"LOOP"};
  juce::TextButton loopHalveButton_{"/2"};
  juce::TextButton loopDoubleButton_{"x2"};
  std::array<juce::TextButton, 4> beatLoopButtons_{
      juce::TextButton{"1"}, juce::TextButton{"2"}, juce::TextButton{"4"}, juce::TextButton{"8"}};

  // Transport Controls
  juce::TextButton cueButton_{"CUE"};
  juce::TextButton playButton_{"PLAY"};
  juce::TextButton syncButton_{"SYNC"};

  // Pitch / Tempo Slider
  juce::Slider pitchSlider_;
  juce::Label pitchLabel_;
  juce::TextButton pitchBendPlus_{"+"};
  juce::TextButton pitchBendMinus_{"-"};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeckComponent)
};

}  // namespace zyron::ui
