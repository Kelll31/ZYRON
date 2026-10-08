// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Deck/DeckComponent.hpp"
#include "UI/Deck/KeyMath.hpp"
#include "UI/Localization.hpp"

#include <iterator>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace zyron::ui {

namespace {

std::string formatTime(double seconds) {
  if (seconds < 0.0) seconds = 0.0;
  const int totalSec = static_cast<int>(seconds);
  const int min = totalSec / 60;
  const int sec = totalSec % 60;
  const int tenths = static_cast<int>((seconds - totalSec) * 10.0);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d.%1d", min, sec, tenths);
  return std::string(buf);
}

}  // namespace

DeckComponent::DeckComponent(core::DeckId deckId, Theme theme)
    : deckId_(deckId), theme_(theme), waveformView_(theme) {
  setupHeader();
  setupTransport();
  setupLoops();
  setupHotCues();
  setupStems();
  setupPitchFader();
  setupKeyRow();

  waveformView_.setMode(WaveformView::Mode::DetailOnly);
  waveformView_.onSeekRequested = [this](double sec) {
    if (onSeekRequested) {
      onSeekRequested(deckId_, sec);
    }
  };
  waveformView_.onCueTriggered = [this](int cueIdx) {
    if (onHotCueClicked) {
      onHotCueClicked(deckId_, cueIdx);
    }
  };
  addAndMakeVisible(waveformView_);

  setTheme(theme);
}

void DeckComponent::setupHeader() {
  const juce::Colour deckCol = theme_.deckColour(deckId_);
  juce::String badgeText = juce::String(TRANS("DECK")) + " ";
  switch (deckId_) {
    case core::DeckId::A:
      badgeText += "A";
      break;
    case core::DeckId::B:
      badgeText += "B";
      break;
    case core::DeckId::C:
      badgeText += "C";
      break;
    case core::DeckId::D:
      badgeText += "D";
      break;
  }

  deckBadgeLabel_.setText(badgeText, juce::dontSendNotification);
  deckBadgeLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
  deckBadgeLabel_.setColour(juce::Label::textColourId, deckCol);
  deckBadgeLabel_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(deckBadgeLabel_);

  titleLabel_.setText(TRANS("No Track Loaded"), juce::dontSendNotification);
  titleLabel_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
  titleLabel_.setColour(juce::Label::textColourId, theme_.text);
  titleLabel_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(titleLabel_);

  artistLabel_.setText("-", juce::dontSendNotification);
  artistLabel_.setFont(juce::FontOptions(13.0f));
  artistLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  artistLabel_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(artistLabel_);

  bpmLabel_.setText("---.-- BPM", juce::dontSendNotification);
  bpmLabel_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
  bpmLabel_.setColour(juce::Label::textColourId, theme_.text);
  bpmLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(bpmLabel_);

  keyLabel_.setText("--", juce::dontSendNotification);
  keyLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
  keyLabel_.setColour(juce::Label::textColourId, theme_.accent);
  keyLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(keyLabel_);

  splitButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  splitButton_.setColour(juce::TextButton::textColourOffId, theme_.accent);
  splitButton_.setTooltip(TRANS("Separate the track into vocals, drums, bass and other (neural network, runs in the background)"));
  splitButton_.onClick = [this] {
    if (onSeparateStemsClicked) onSeparateStemsClicked(deckId_);
  };
  addAndMakeVisible(splitButton_);

  markButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  markButton_.setColour(juce::TextButton::textColourOffId, theme_.text);
  markButton_.setTooltip(TRANS("Mark the playhead as a mix point, drop or breakdown (the AI proposes them automatically)"));
  markButton_.onClick = [this] { showMarkerMenu(); };
  addAndMakeVisible(markButton_);

  scratchButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  scratchButton_.setColour(juce::TextButton::textColourOffId, theme_.text);
  scratchButton_.setTooltip(TRANS("Scratch (the deck performs it in time with the track)"));
  scratchButton_.onClick = [this] { showScratchMenu(); };
  addAndMakeVisible(scratchButton_);

  timeElapsedLabel_.setText("00:00.0", juce::dontSendNotification);
  timeElapsedLabel_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
  timeElapsedLabel_.setColour(juce::Label::textColourId, theme_.text);
  timeElapsedLabel_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(timeElapsedLabel_);

  timeRemainingLabel_.setText("-00:00.0", juce::dontSendNotification);
  timeRemainingLabel_.setFont(juce::FontOptions(14.0f));
  timeRemainingLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  timeRemainingLabel_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(timeRemainingLabel_);
}

void DeckComponent::setupTransport() {
  cueButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  cueButton_.setColour(juce::TextButton::textColourOffId, theme_.cueActive);
  cueButton_.onClick = [this] {
    if (onCueClicked) onCueClicked(deckId_);
  };
  addAndMakeVisible(cueButton_);

  playButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  playButton_.setColour(juce::TextButton::textColourOffId, theme_.playActive);
  playButton_.onClick = [this] {
    if (telemetry_.isPlaying) {
      if (onPauseClicked) onPauseClicked(deckId_);
    } else {
      if (onPlayClicked) onPlayClicked(deckId_);
    }
  };
  addAndMakeVisible(playButton_);

  syncButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  syncButton_.setColour(juce::TextButton::textColourOffId, theme_.syncActive);
  syncButton_.onClick = [this] {
    if (onSyncClicked) onSyncClicked(deckId_);
  };
  addAndMakeVisible(syncButton_);
}

void DeckComponent::setupLoops() {
  loopInButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  loopInButton_.setColour(juce::TextButton::textColourOffId, theme_.text);
  loopInButton_.onClick = [this] {
    if (onLoopInClicked) onLoopInClicked(deckId_);
  };
  addAndMakeVisible(loopInButton_);

  loopOutButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  loopOutButton_.setColour(juce::TextButton::textColourOffId, theme_.text);
  loopOutButton_.onClick = [this] {
    if (onLoopOutClicked) onLoopOutClicked(deckId_);
  };
  addAndMakeVisible(loopOutButton_);

  loopActiveButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  loopActiveButton_.setColour(juce::TextButton::textColourOffId, theme_.accent);
  loopActiveButton_.onClick = [this] {
    if (onLoopToggleClicked) onLoopToggleClicked(deckId_);
  };
  addAndMakeVisible(loopActiveButton_);

  loopHalveButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  loopHalveButton_.setColour(juce::TextButton::textColourOffId, theme_.textDim);
  loopHalveButton_.onClick = [this] {
    const double curBeats = (telemetry_.loop.active && telemetry_.beatgrid.beatIntervalSec > 0.0)
                                ? ((telemetry_.loop.endTimeSec - telemetry_.loop.startTimeSec) /
                                   telemetry_.beatgrid.beatIntervalSec)
                                : 4.0;
    if (onBeatLoopClicked) onBeatLoopClicked(deckId_, std::max(0.25, curBeats * 0.5));
  };
  addAndMakeVisible(loopHalveButton_);

  loopDoubleButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  loopDoubleButton_.setColour(juce::TextButton::textColourOffId, theme_.textDim);
  loopDoubleButton_.onClick = [this] {
    const double curBeats = (telemetry_.loop.active && telemetry_.beatgrid.beatIntervalSec > 0.0)
                                ? ((telemetry_.loop.endTimeSec - telemetry_.loop.startTimeSec) /
                                   telemetry_.beatgrid.beatIntervalSec)
                                : 4.0;
    if (onBeatLoopClicked) onBeatLoopClicked(deckId_, std::min(64.0, curBeats * 2.0));
  };
  addAndMakeVisible(loopDoubleButton_);

  const std::array<double, 4> beatSizes{1.0, 2.0, 4.0, 8.0};
  for (std::size_t i = 0; i < beatLoopButtons_.size(); ++i) {
    auto& btn = beatLoopButtons_[i];
    btn.setColour(juce::TextButton::buttonColourId, theme_.panel);
    btn.setColour(juce::TextButton::textColourOffId, theme_.text);
    const double beats = beatSizes[i];
    btn.onClick = [this, beats] {
      if (onBeatLoopClicked) onBeatLoopClicked(deckId_, beats);
    };
    addAndMakeVisible(btn);
  }
}

void DeckComponent::setupHotCues() {
  for (int i = 0; i < kNumCues; ++i) {
    auto& btn = hotCueButtons_[static_cast<std::size_t>(i)];
    btn.setColour(juce::TextButton::buttonColourId, theme_.panel);
    btn.setColour(juce::TextButton::textColourOffId, theme_.textDim);
    btn.onClick = [this, i] {
      if (onHotCueClicked) onHotCueClicked(deckId_, i);
    };
    addAndMakeVisible(btn);
  }
}

void DeckComponent::setupStems() {
  const std::array<juce::Colour, kNumStems> stemColors{
      juce::Colour{0xffff3399},  // Vocals: Pink
      juce::Colour{0xffffcc00},  // Drums: Yellow
      juce::Colour{0xff00d2ff},  // Bass: Cyan
      juce::Colour{0xff00ff88}   // Other: Green
  };

  for (int i = 0; i < kNumStems; ++i) {
    auto& btn = stemMuteButtons_[static_cast<std::size_t>(i)];
    btn.setClickingTogglesState(true);
    btn.setColour(juce::TextButton::buttonColourId, theme_.panel);
    btn.setColour(juce::TextButton::buttonOnColourId, stemColors[static_cast<std::size_t>(i)].withAlpha(0.3f));
    btn.setColour(juce::TextButton::textColourOffId, stemColors[static_cast<std::size_t>(i)]);
    btn.setColour(juce::TextButton::textColourOnId, theme_.textDim);
    btn.onClick = [this, i, &btn] {
      if (onStemMuteChanged) onStemMuteChanged(deckId_, i, btn.getToggleState());
      // Without stems yet the click also starts the separation; the mute is kept and applies when the stems arrive.
      if (!telemetry_.hasStems && splitButton_.isEnabled() && onSeparateStemsClicked) onSeparateStemsClicked(deckId_);
    };
    addAndMakeVisible(btn);

    auto& slider = stemVolumeSliders_[static_cast<std::size_t>(i)];
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(0.0, 1.0, 0.01);
    slider.setValue(1.0, juce::dontSendNotification);
    slider.setDefaultValue(1.0);
    slider.setParameterName(TRANS("Stem volume (0 to 1)"));
    slider.setColour(juce::Slider::thumbColourId, stemColors[static_cast<std::size_t>(i)]);
    slider.setColour(juce::Slider::trackColourId, stemColors[static_cast<std::size_t>(i)].withAlpha(0.45f));
    slider.setColour(juce::Slider::backgroundColourId, theme_.panel.brighter(0.15f));
    slider.onValueChange = [this, i, &slider] {
      if (onStemVolumeChanged) onStemVolumeChanged(deckId_, i, static_cast<float>(slider.getValue()));
    };
    addAndMakeVisible(slider);
  }
  stemsStatusLabel_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
  stemsStatusLabel_.setJustificationType(juce::Justification::centred);
  stemsStatusLabel_.setColour(juce::Label::textColourId, theme_.text);
  stemsStatusLabel_.setColour(juce::Label::backgroundColourId, theme_.background.withAlpha(0.8f));
  stemsStatusLabel_.setInterceptsMouseClicks(false, false);
  addChildComponent(stemsStatusLabel_);
}

void DeckComponent::setupPitchFader() {
  pitchSlider_.setSliderStyle(juce::Slider::LinearVertical);
  pitchSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  pitchSlider_.setRange(-16.0, 16.0, 0.05);  // ±16%: room for a sync across neighbouring tempos
  pitchSlider_.setValue(0.0, juce::dontSendNotification);
  pitchSlider_.setDefaultValue(0.0);
  pitchSlider_.setParameterName(TRANS("Pitch (%)"));
  pitchSlider_.setColour(juce::Slider::thumbColourId, theme_.accent);
  pitchSlider_.setColour(juce::Slider::trackColourId, theme_.panel);
  pitchSlider_.onValueChange = [this] {
    const double pitchPercent = pitchSlider_.getValue();
    const double speedRatio = 1.0 + (pitchPercent / 100.0);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%+.1f%%", pitchPercent);
    pitchLabel_.setText(buf, juce::dontSendNotification);
    if (onPitchChanged) onPitchChanged(deckId_, speedRatio);
  };
  addAndMakeVisible(pitchSlider_);

  pitchLabel_.setText("+0.0%", juce::dontSendNotification);
  pitchLabel_.setFont(juce::FontOptions(11.0f));
  pitchLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  pitchLabel_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(pitchLabel_);

  pitchBendPlus_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  pitchBendPlus_.setColour(juce::TextButton::textColourOffId, theme_.text);
  pitchBendPlus_.onClick = [this] {
    pitchSlider_.setValue(pitchSlider_.getValue() + 0.1, juce::sendNotificationSync);
  };
  addAndMakeVisible(pitchBendPlus_);

  pitchBendMinus_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  pitchBendMinus_.setColour(juce::TextButton::textColourOffId, theme_.text);
  pitchBendMinus_.onClick = [this] {
    pitchSlider_.setValue(pitchSlider_.getValue() - 0.1, juce::sendNotificationSync);
  };
  addAndMakeVisible(pitchBendMinus_);
}

void DeckComponent::setupKeyRow() {
  keylockButton_.setClickingTogglesState(true);
  keylockButton_.setToggleState(true, juce::dontSendNotification);
  keylockButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  keylockButton_.setColour(juce::TextButton::buttonOnColourId, theme_.accent.withAlpha(0.3f));
  keylockButton_.setColour(juce::TextButton::textColourOffId, theme_.textDim);
  keylockButton_.setColour(juce::TextButton::textColourOnId, theme_.accent);
  keylockButton_.setTooltip(TRANS("Keylock (master tempo): changing the tempo keeps the pitch. Off: like a turntable"));
  keylockButton_.onClick = [this] {
    keylock_ = keylockButton_.getToggleState();
    refreshKeyDisplay();
    if (onKeylockChanged) onKeylockChanged(deckId_, keylock_);
  };
  addAndMakeVisible(keylockButton_);

  keyShiftMinus_.setTooltip(TRANS("Key down one semitone (the tempo stays)"));
  keyShiftPlus_.setTooltip(TRANS("Key up one semitone (the tempo stays)"));
  keyShiftValue_.setTooltip(TRANS("Key shift in semitones | Click: back to 0"));
  keyShiftMinus_.onClick = [this] { nudgeKeyShift(-1.0F); };
  keyShiftPlus_.onClick = [this] { nudgeKeyShift(1.0F); };
  keyShiftValue_.onClick = [this] {
    keyShift_ = 0.0F;
    refreshKeyDisplay();
    if (onKeyShiftChanged) onKeyShiftChanged(deckId_, keyShift_);
  };
  for (auto* button : {&keyShiftMinus_, &keyShiftPlus_, &keyShiftValue_}) {
    button->setColour(juce::TextButton::buttonColourId, theme_.panel);
    button->setColour(juce::TextButton::textColourOffId, theme_.text);
    addAndMakeVisible(*button);
  }

  effectiveKeyLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
  effectiveKeyLabel_.setColour(juce::Label::textColourId, theme_.accent);
  effectiveKeyLabel_.setJustificationType(juce::Justification::centredRight);
  effectiveKeyLabel_.setMinimumHorizontalScale(0.8f);
  effectiveKeyLabel_.setTooltip(
      TRANS("The key you hear: the key of the track moved by the key shift (and by the tempo when keylock is off)"));
  addAndMakeVisible(effectiveKeyLabel_);

  fxButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  fxButton_.setColour(juce::TextButton::textColourOffId, theme_.textDim);
  fxButton_.setTooltip(TRANS("Channel effects: two slots (echo, reverb, flanger, phaser, delay)"));
  fxButton_.onClick = [this] {
    if (onFxButtonClicked) onFxButtonClicked(deckId_, fxButton_);
  };
  addAndMakeVisible(fxButton_);
  refreshKeyDisplay();
}

void DeckComponent::nudgeKeyShift(float semitones) {
  keyShift_ = std::clamp(keyShift_ + semitones, -6.0F, 6.0F);
  refreshKeyDisplay();
  if (onKeyShiftChanged) onKeyShiftChanged(deckId_, keyShift_);
}

void DeckComponent::refreshKeyDisplay() {
  keyShiftValue_.setButtonText(formatSemitones(keyShift_));
  const double heard = effectiveSemitones(keyShift_, keylock_, telemetry_.playbackSpeed);
  const std::string shifted = shiftCamelot(track_.key, heard);
  juce::String text = track_.key.empty() ? juce::String("--") : juce::String(shifted);
  if (!track_.key.empty() && shifted != track_.key) {
    text = juce::String(track_.key) + " " + juce::String::charToString(0x2192) + " " + text;
  }
  effectiveKeyLabel_.setText(text, juce::dontSendNotification);
}

void DeckComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  const juce::Colour deckCol = theme_.deckColour(deckId_);
  deckBadgeLabel_.setColour(juce::Label::textColourId, deckCol);
  titleLabel_.setColour(juce::Label::textColourId, theme_.text);
  artistLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  bpmLabel_.setColour(juce::Label::textColourId, theme_.text);
  keyLabel_.setColour(juce::Label::textColourId, theme_.accent);
  timeElapsedLabel_.setColour(juce::Label::textColourId, theme_.text);
  timeRemainingLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  effectiveKeyLabel_.setColour(juce::Label::textColourId, theme_.accent);
  keylockButton_.setColour(juce::TextButton::buttonOnColourId, theme_.accent.withAlpha(0.3f));
  keylockButton_.setColour(juce::TextButton::textColourOnId, theme_.accent);
  keylockButton_.setColour(juce::TextButton::textColourOffId, theme_.textDim);

  waveformView_.setTheme(theme);
  repaint();
}

void DeckComponent::setTrack(const core::TrackItem& track, core::WaveformData waveform) {
  updateTrackInfo(track);
  waveformView_.setWaveformData(std::move(waveform));
  updateTimeLabels(0.0, track.durationSec);
}

void DeckComponent::updateTrackInfo(const core::TrackItem& track) {
  track_ = track;
  titleLabel_.setText(track.title.empty() ? juce::String(TRANS("Untitled Track")) : juce::String(track.title),
                      juce::dontSendNotification);
  artistLabel_.setText(track.artist.empty() ? juce::String("-") : juce::String(track.artist),
                       juce::dontSendNotification);

  if (track.bpm > 0.0) {
    char bpmBuf[32];
    std::snprintf(bpmBuf, sizeof(bpmBuf), "%.2f BPM", track.bpm);
    bpmLabel_.setText(bpmBuf, juce::dontSendNotification);
  } else {
    bpmLabel_.setText("---.-- BPM", juce::dontSendNotification);
  }

  keyLabel_.setText(track.key.empty() ? juce::String("--") : juce::String(track.key),
                    juce::dontSendNotification);
  refreshKeyDisplay();
}

void DeckComponent::setWaveformData(core::WaveformData waveform) {
  waveformView_.setWaveformData(std::move(waveform));
}

void DeckComponent::showScratchMenu() {
  struct Pattern {
    core::ScratchPattern pattern;
    const char* name;
  };
  static const Pattern kPatterns[] = {
      {core::ScratchPattern::Baby, "Baby scratch"}, {core::ScratchPattern::Transformer, "Transformer"},
      {core::ScratchPattern::Chirp, "Chirp"},       {core::ScratchPattern::Flare, "Flare"},
      {core::ScratchPattern::Crab, "Crab"},         {core::ScratchPattern::Scribble, "Scribble"},
      {core::ScratchPattern::Tear, "Tear"},         {core::ScratchPattern::Stab, "Stab"},
      {core::ScratchPattern::Drag, "Drag"},         {core::ScratchPattern::Backspin, "Backspin (stops the deck)"},
  };
  // Menu id = pattern index * 10 + beats (1 or 2); a backspin is a single beat.
  juce::PopupMenu menu;
  menu.addSectionHeader(TRANS("SCR"));
  for (std::size_t i = 0; i < std::size(kPatterns); ++i) {
    const bool backspin = kPatterns[i].pattern == core::ScratchPattern::Backspin;
    if (backspin) {
      menu.addSeparator();
    }
    for (int beats = 1; beats <= (backspin ? 1 : 2); ++beats) {
      menu.addItem(static_cast<int>(i) * 10 + beats,
                   juce::translate(kPatterns[i].name) + ", " + juce::translate(beats == 1 ? "1 beat" : "2 beats"));
    }
  }
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&scratchButton_), [this](int choice) {
    const auto index = static_cast<std::size_t>(choice / 10);
    if (choice > 0 && index < std::size(kPatterns) && onScratchRequested) {
      onScratchRequested(deckId_, kPatterns[index].pattern, static_cast<double>(choice % 10));
    }
  });
}

void DeckComponent::showMarkerMenu() {
  juce::PopupMenu menu;
  menu.addSectionHeader(TRANS("Mark the playhead as"));
  menu.addItem(1, TRANS("Mix in (where this track may come in)"));
  menu.addItem(2, TRANS("Mix out (where the mix out should begin)"));
  menu.addItem(3, TRANS("Drop"));
  menu.addItem(4, TRANS("Breakdown"));
  menu.addItem(5, TRANS("Intro"));
  menu.addItem(6, TRANS("Outro"));
  menu.addSeparator();
  menu.addItem(10, TRANS("Remove the marker nearest to the playhead"));

  // Jump to a marker: the AI's proposals and the user's own, in track order.
  const auto markers = telemetry_.markers;
  if (!markers.empty()) {
    menu.addSeparator();
    menu.addSectionHeader(TRANS("Jump to"));
    for (std::size_t i = 0; i < markers.size(); ++i) {
      const int total = static_cast<int>(markers[i].timeSec);
      char time[16];
      std::snprintf(time, sizeof(time), "%d:%02d", total / 60, total % 60);
      menu.addItem(100 + static_cast<int>(i), juce::String(markers[i].name) + juce::String("   ") + juce::String(time));
    }
  }
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&markButton_), [this, markers](int choice) {
    if (choice >= 100 && choice < 100 + static_cast<int>(markers.size())) {
      if (onSeekRequested) {
        onSeekRequested(deckId_, markers[static_cast<std::size_t>(choice - 100)].timeSec);
      }
      return;
    }
    static const char* const kTypes[] = {"", "mix_in", "mix_out", "drop", "break", "intro", "outro"};
    if (choice >= 1 && choice <= 6 && onMarkerAddRequested) {
      onMarkerAddRequested(deckId_, kTypes[choice]);
    } else if (choice == 10 && onMarkerRemoveNearestRequested) {
      onMarkerRemoveNearestRequested(deckId_);
    }
  });
}

void DeckComponent::setStemStatus(core::StemPhase phase, float progress, const std::string& message) {
  juce::String text = "SPLIT";
  bool enabled = true;
  juce::String tip = TRANS("Separate the track into vocals, drums, bass and other (runs in the background)");
  switch (phase) {
    case core::StemPhase::None:
      break;
    case core::StemPhase::Queued:
      text = "SPLIT...";
      enabled = false;
      tip = TRANS("Waiting for the stem separation to start");
      break;
    case core::StemPhase::Running:
      text = "SPLIT " + juce::String(static_cast<int>(progress * 100.0F)) + "%";
      enabled = false;
      tip = TRANS("Separating the track with the neural network");
      break;
    case core::StemPhase::Ready:
      text = "STEMS";
      enabled = false;
      tip = TRANS("The deck plays from its stems: use the VOC / DRUM / BASS / OTHER controls");
      break;
    case core::StemPhase::Failed:
      text = "SPLIT !";
      tip = juce::String(TRANS("Stem separation failed:")) + " " + i18n::translateMessage(message);
      break;
  }
  splitButton_.setButtonText(text);
  splitButton_.setEnabled(enabled);
  splitButton_.setTooltip(tip);

  // The stem controls only act on separated stems: say so over them until the stems are there.
  juce::String status;
  switch (phase) {
    case core::StemPhase::None: status = TRANS("Click a stem button to separate the track"); break;
    case core::StemPhase::Queued: status = TRANS("Stems: waiting to start"); break;
    case core::StemPhase::Running:
      status = TRANS("Stems: preparing %n%").replace("%n", juce::String(static_cast<int>(progress * 100.0F)));
      break;
    case core::StemPhase::Ready: break;
    case core::StemPhase::Failed: status = TRANS("Stems failed: click SPLIT to retry"); break;
  }
  stemsStatusLabel_.setText(status, juce::dontSendNotification);
  stemsStatusLabel_.setVisible(status.isNotEmpty() && track_.id > 0);
}

void DeckComponent::updateTimeLabels(double currentSec, double durationSec) {
  timeElapsedLabel_.setText(formatTime(currentSec), juce::dontSendNotification);
  const double remain = (durationSec > currentSec) ? (durationSec - currentSec) : 0.0;
  timeRemainingLabel_.setText("-" + formatTime(remain), juce::dontSendNotification);
}

void DeckComponent::syncFromState(const core::DeckState& state) {
  // Keylock, key shift and the effect slots: Automix and MIDI change them too.
  keylock_ = state.keylock;
  if (!keylockButton_.isMouseButtonDown()) {
    keylockButton_.setToggleState(state.keylock, juce::dontSendNotification);
  }
  keyShift_ = state.keyShift;
  refreshKeyDisplay();
  const bool fxActive =
      std::any_of(state.fx.begin(), state.fx.end(), [](const core::FxSlotState& fx) { return fx.enabled; });
  fxButton_.setColour(juce::TextButton::buttonColourId, fxActive ? theme_.accent.withAlpha(0.3f) : theme_.panel);
  fxButton_.setColour(juce::TextButton::textColourOffId, fxActive ? theme_.accent : theme_.textDim);

  for (std::size_t i = 0; i < stemMuteButtons_.size() && i < state.stems.size(); ++i) {
    stemVolumeSliders_[i].showExternalValue(state.stems[i].volume);
    if (stemMuteButtons_[i].getToggleState() != state.stems[i].muted && !stemMuteButtons_[i].isMouseButtonDown()) {
      stemMuteButtons_[i].setToggleState(state.stems[i].muted, juce::dontSendNotification);
    }
  }
}

void DeckComponent::updateTelemetry(const core::DeckTelemetry& telemetry) {
  telemetry_ = telemetry;
  waveformView_.updateTelemetry(telemetry);

  // The stem controls stay usable: before the stems exist a click starts the separation, and the settings made in the
  // meantime apply as soon as the deck switches to its stems. Dimmed until then, so it is clear they act later.
  for (std::size_t i = 0; i < stemMuteButtons_.size(); ++i) {
    stemMuteButtons_[i].setAlpha(telemetry.hasStems ? 1.0f : 0.55f);
    stemVolumeSliders_[i].setAlpha(telemetry.hasStems ? 1.0f : 0.55f);
  }

  // The pitch fader follows what the deck really plays at (a sync moves it), unless the user is holding it.
  if (!pitchSlider_.isMouseButtonDown()) {
    const double pitchPercent = (telemetry.playbackSpeed - 1.0) * 100.0;
    pitchSlider_.setValue(pitchPercent, juce::dontSendNotification);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%+.1f%%", pitchPercent);
    pitchLabel_.setText(buf, juce::dontSendNotification);
  }

  // Play button state
  if (telemetry.isPlaying) {
    playButton_.setButtonText("PAUSE");
    playButton_.setColour(juce::TextButton::buttonColourId, theme_.playActive.withAlpha(0.2f));
    playButton_.setColour(juce::TextButton::textColourOffId, theme_.playActive);
  } else {
    playButton_.setButtonText("PLAY");
    playButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
    playButton_.setColour(juce::TextButton::textColourOffId, theme_.playActive);
  }

  // Loop button highlight
  if (telemetry.loop.active) {
    loopActiveButton_.setColour(juce::TextButton::buttonColourId, theme_.accent.withAlpha(0.3f));
  } else {
    loopActiveButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  }

  // Hot cue buttons color indicators
  for (int i = 0; i < kNumCues; ++i) {
    auto& btn = hotCueButtons_[static_cast<std::size_t>(i)];
    if (telemetry.hotCues[static_cast<std::size_t>(i)].has_value()) {
      const auto& cue = *telemetry.hotCues[static_cast<std::size_t>(i)];
      btn.setColour(juce::TextButton::buttonColourId, theme_.panel.brighter(0.2f));
      btn.setColour(juce::TextButton::textColourOffId,
                    cue.color.empty() ? theme_.accent : juce::Colour::fromString(cue.color));
    } else {
      btn.setColour(juce::TextButton::buttonColourId, theme_.panel);
      btn.setColour(juce::TextButton::textColourOffId, theme_.textDim);
    }
  }

  updateTimeLabels(telemetry.currentTimeSec, telemetry.durationSec);
}

void DeckComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.panel);

  // Deck color accent top border
  const juce::Colour deckCol = (deckId_ == core::DeckId::A) ? theme_.deckA : theme_.deckB;
  g.setColour(deckCol);
  g.fillRect(getLocalBounds().removeFromTop(3));

  // Subtle panel boundary
  g.setColour(theme_.background);
  g.drawRect(getLocalBounds(), 1);
}

void DeckComponent::resized() {
  auto area = getLocalBounds().reduced(8);
  area.removeFromTop(3);  // top color border

  // 1. Header (Height 54px)
  auto headerArea = area.removeFromTop(54);
  auto headerLeft = headerArea.removeFromLeft(headerArea.getWidth() * 2 / 3);
  auto headerRight = headerArea;

  auto badgeAndTitle = headerLeft.removeFromTop(24);
  deckBadgeLabel_.setBounds(badgeAndTitle.removeFromLeft(54));
  titleLabel_.setBounds(badgeAndTitle);

  auto artistAndTime = headerLeft;
  artistLabel_.setBounds(artistAndTime.removeFromTop(18));
  auto timeRow = artistAndTime;
  timeElapsedLabel_.setBounds(timeRow.removeFromLeft(80));
  timeRemainingLabel_.setBounds(timeRow);

  auto bpmRow = headerRight.removeFromTop(24);
  bpmLabel_.setBounds(bpmRow);
  auto keyRow = headerRight.removeFromTop(20);
  const int rowWidth = keyRow.getWidth();
  splitButton_.setBounds(keyRow.removeFromLeft(rowWidth * 3 / 10).reduced(1, 0));
  markButton_.setBounds(keyRow.removeFromLeft(rowWidth * 3 / 10).reduced(1, 0));
  scratchButton_.setBounds(keyRow.removeFromLeft(rowWidth * 2 / 10).reduced(1, 0));
  keyLabel_.setBounds(keyRow);

  area.removeFromTop(4);

  // 2. Waveform View (Height ~100px) and the key row under it. A short deck (4-deck layout) gives the row's height
  // to the waveform so the buttons below keep their size.
  constexpr int kKeyRowHeight = 22;
  int waveformH = std::max(70, area.getHeight() * 35 / 100);
  if (getHeight() < 240) {
    waveformH = std::max(48, waveformH - kKeyRowHeight);
  }
  waveformView_.setBounds(area.removeFromTop(waveformH));
  area.removeFromTop(2);
  auto soundRow = area.removeFromTop(kKeyRowHeight);
  keylockButton_.setBounds(soundRow.removeFromLeft(58).reduced(1, 1));
  keyShiftMinus_.setBounds(soundRow.removeFromLeft(22).reduced(1, 1));
  keyShiftValue_.setBounds(soundRow.removeFromLeft(48).reduced(1, 1));
  keyShiftPlus_.setBounds(soundRow.removeFromLeft(22).reduced(1, 1));
  fxButton_.setBounds(soundRow.removeFromRight(34).reduced(1, 1));
  effectiveKeyLabel_.setBounds(soundRow);

  area.removeFromTop(4);

  // 3. Right: Pitch Fader Strip (Width 44px)
  auto pitchArea = area.removeFromRight(44);
  pitchLabel_.setBounds(pitchArea.removeFromTop(16));
  pitchBendPlus_.setBounds(pitchArea.removeFromTop(20).reduced(2, 1));
  pitchBendMinus_.setBounds(pitchArea.removeFromBottom(20).reduced(2, 1));
  pitchSlider_.setBounds(pitchArea.reduced(2, 0));

  area.removeFromRight(6);

  // 4. Middle Controls Area
  auto controlsArea = area;
  const int halfH = controlsArea.getHeight() / 2;
  auto topControls = controlsArea.removeFromTop(halfH);
  auto bottomControls = controlsArea;

  // Top Controls: Stems (left) + Hot Cues (right)
  auto stemsArea = topControls.removeFromLeft(topControls.getWidth() * 45 / 100);
  const auto stemsBounds = stemsArea;
  const int stemColW = stemsArea.getWidth() / kNumStems;
  for (int i = 0; i < kNumStems; ++i) {
    auto col = stemsArea.removeFromLeft(stemColW).reduced(2, 1);
    stemMuteButtons_[static_cast<std::size_t>(i)].setBounds(col.removeFromTop(20));
    stemVolumeSliders_[static_cast<std::size_t>(i)].setBounds(col);
  }
  stemsStatusLabel_.setBounds(stemsBounds.withTrimmedTop(24).reduced(4, 0));

  topControls.removeFromLeft(6);
  // Hot Cues 1..8 in 2 rows of 4
  auto cueArea = topControls;
  const int cueRowH = cueArea.getHeight() / 2;
  auto cueRow1 = cueArea.removeFromTop(cueRowH);
  auto cueRow2 = cueArea;
  const int cueColW = cueArea.getWidth() / 4;
  for (int i = 0; i < 4; ++i) {
    hotCueButtons_[static_cast<std::size_t>(i)].setBounds(cueRow1.removeFromLeft(cueColW).reduced(2, 1));
    hotCueButtons_[static_cast<std::size_t>(i + 4)].setBounds(cueRow2.removeFromLeft(cueColW).reduced(2, 1));
  }

  bottomControls.removeFromTop(4);

  // Bottom Controls: Loops (left) + Transport (right)
  auto loopsArea = bottomControls.removeFromLeft(bottomControls.getWidth() * 55 / 100);
  const int loopRowH = loopsArea.getHeight() / 2;
  auto loopRow1 = loopsArea.removeFromTop(loopRowH);
  auto loopRow2 = loopsArea;

  // Row 1: IN, OUT, LOOP, /2, x2
  const int loopBtnW1 = loopRow1.getWidth() / 5;
  loopInButton_.setBounds(loopRow1.removeFromLeft(loopBtnW1).reduced(2, 1));
  loopOutButton_.setBounds(loopRow1.removeFromLeft(loopBtnW1).reduced(2, 1));
  loopActiveButton_.setBounds(loopRow1.removeFromLeft(loopBtnW1).reduced(2, 1));
  loopHalveButton_.setBounds(loopRow1.removeFromLeft(loopBtnW1).reduced(2, 1));
  loopDoubleButton_.setBounds(loopRow1.removeFromLeft(loopBtnW1).reduced(2, 1));

  // Row 2: 1, 2, 4, 8 beats
  const int loopBtnW2 = loopRow2.getWidth() / 4;
  for (std::size_t i = 0; i < beatLoopButtons_.size(); ++i) {
    beatLoopButtons_[i].setBounds(loopRow2.removeFromLeft(loopBtnW2).reduced(2, 1));
  }

  bottomControls.removeFromLeft(6);

  // Transport: SYNC (small), CUE (large), PLAY (large)
  auto transportArea = bottomControls;
  auto syncArea = transportArea.removeFromTop(20);
  syncButton_.setBounds(syncArea.reduced(2, 1));

  const int transportBtnW = transportArea.getWidth() / 2;
  cueButton_.setBounds(transportArea.removeFromLeft(transportBtnW).reduced(2, 1));
  playButton_.setBounds(transportArea.reduced(2, 1));
}

}  // namespace zyron::ui
