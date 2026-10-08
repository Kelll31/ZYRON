// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <string>

#include "Core/Audio/DeckTelemetry.hpp"
#include "Core/Audio/EngineView.hpp"
#include "Core/Audio/WaveformData.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/AppState.hpp"
#include "Core/State/Ids.hpp"
#include "UI/ParamSlider.hpp"
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
  /// Refreshes the title, BPM and key labels (e.g. after background analysis) without touching the waveform.
  void updateTrackInfo(const core::TrackItem& track);
  /// Shows how the neural stem separation of the loaded track is doing (the SPLIT button).
  void setStemStatus(core::StemPhase phase, float progress, const std::string& message);
  void updateTelemetry(const core::DeckTelemetry& telemetry);
  /// Automix transition regions of the loaded track, drawn on the detail waveform (empty clears them).
  void setTransitionRegions(const std::vector<core::TransitionRegion>& regions) {
    waveformView_.setTransitionRegions(regions);
  }

  /// Mirrors the stem faders and mutes from the engine's state (Automix mutes and brings stems in during a mix).
  void syncFromState(const core::DeckState& state);

  // User Action Callbacks
  std::function<void(core::DeckId deck)> onPlayClicked;
  std::function<void(core::DeckId deck)> onPauseClicked;
  std::function<void(core::DeckId deck)> onCueClicked;
  std::function<void(core::DeckId deck)> onSyncClicked;
  std::function<void(core::DeckId deck)> onSeparateStemsClicked;
  /// Add a marker of `type` (core::TrackMarker::k... names) at the playhead.
  std::function<void(core::DeckId deck, const std::string& type)> onMarkerAddRequested;
  std::function<void(core::DeckId deck)> onMarkerRemoveNearestRequested;
  /// A scratch picked from the SCR menu; the owner turns it into a Command (it knows the tempo).
  std::function<void(core::DeckId deck, core::ScratchPattern pattern, double beats)> onScratchRequested;
  std::function<void(core::DeckId deck, double seekSec)> onSeekRequested;
  std::function<void(core::DeckId deck, int cueIndex)> onHotCueClicked;
  std::function<void(core::DeckId deck)> onLoopInClicked;
  std::function<void(core::DeckId deck)> onLoopOutClicked;
  std::function<void(core::DeckId deck)> onLoopToggleClicked;
  std::function<void(core::DeckId deck, double beats)> onBeatLoopClicked;
  std::function<void(core::DeckId deck, double speedRatio)> onPitchChanged;
  std::function<void(core::DeckId deck, float gainDb)> onGainChanged;
  std::function<void(core::DeckId deck, bool enabled)> onKeylockChanged;
  std::function<void(core::DeckId deck, float semitones)> onKeyShiftChanged;
  /// The FX button was pressed: the owner shows the deck's effect slots next to `target`.
  std::function<void(core::DeckId deck, juce::Component& target)> onFxButtonClicked;
  std::function<void(core::DeckId deck, int stemIndex, float volume)> onStemVolumeChanged;
  std::function<void(core::DeckId deck, int stemIndex, bool muted)> onStemMuteChanged;

  void paint(juce::Graphics& g) override;
  void resized() override;

  [[nodiscard]] WaveformView& waveformView() noexcept { return waveformView_; }

 private:
  void setupHeader();
  void showMarkerMenu();
  void showScratchMenu();
  void setupTransport();
  void setupLoops();
  void setupHotCues();
  void setupStems();
  void setupPitchFader();
  void setupKeyRow();
  void nudgeKeyShift(float semitones);
  void refreshKeyDisplay();
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
  juce::TextButton splitButton_{"SPLIT"};
  juce::TextButton markButton_{TRANS("MARK")};
  juce::TextButton scratchButton_{TRANS("SCR")};
  juce::Label timeElapsedLabel_;
  juce::Label timeRemainingLabel_;

  // Waveform
  WaveformView waveformView_;

  // Stems strip (VOC, DRUM, BASS, OTHER - SPEC §61)
  static constexpr int kNumStems = 4;
  std::array<juce::TextButton, kNumStems> stemMuteButtons_{
      juce::TextButton{"VOC"}, juce::TextButton{"DRUM"}, juce::TextButton{"BASS"}, juce::TextButton{"OTHER"}};
  std::array<ParamSlider, kNumStems> stemVolumeSliders_{};

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

  // Key row: keylock, key shift in semitones, the key that is heard, the FX slots
  juce::TextButton keylockButton_{"KEYLOCK"};
  juce::TextButton keyShiftMinus_{"-"};
  juce::TextButton keyShiftValue_;
  juce::TextButton keyShiftPlus_{"+"};
  juce::Label effectiveKeyLabel_;
  juce::Label stemsStatusLabel_;  // over the stem faders while the deck has no stems yet: why they do nothing
  juce::TextButton fxButton_{"FX"};
  float keyShift_{0.0F};
  bool keylock_{true};

  // Pitch / Tempo Slider
  ParamSlider pitchSlider_;
  juce::Label pitchLabel_;
  juce::TextButton pitchBendPlus_{"+"};
  juce::TextButton pitchBendMinus_{"-"};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeckComponent)
};

}  // namespace zyron::ui
