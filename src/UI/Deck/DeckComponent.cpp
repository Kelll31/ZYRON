// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Deck/DeckComponent.hpp"

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
  juce::String badgeText = "DECK ";
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

  titleLabel_.setText("No Track Loaded", juce::dontSendNotification);
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
    };
    addAndMakeVisible(btn);

    auto& slider = stemVolumeSliders_[static_cast<std::size_t>(i)];
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(0.0, 1.0, 0.01);
    slider.setValue(1.0, juce::dontSendNotification);
    slider.setColour(juce::Slider::thumbColourId, stemColors[static_cast<std::size_t>(i)]);
    slider.setColour(juce::Slider::trackColourId, theme_.panel);
    slider.onValueChange = [this, i, &slider] {
      if (onStemVolumeChanged) onStemVolumeChanged(deckId_, i, static_cast<float>(slider.getValue()));
    };
    addAndMakeVisible(slider);
  }
}

void DeckComponent::setupPitchFader() {
  pitchSlider_.setSliderStyle(juce::Slider::LinearVertical);
  pitchSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  pitchSlider_.setRange(-8.0, 8.0, 0.05);  // ±8% standard DJ range
  pitchSlider_.setValue(0.0, juce::dontSendNotification);
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

  waveformView_.setTheme(theme);
  repaint();
}

void DeckComponent::setTrack(const core::TrackItem& track, core::WaveformData waveform) {
  track_ = track;
  titleLabel_.setText(track.title.empty() ? juce::String("Untitled Track") : juce::String(track.title),
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

  waveformView_.setWaveformData(std::move(waveform));
  updateTimeLabels(0.0, track.durationSec);
}

void DeckComponent::setWaveformData(core::WaveformData waveform) {
  waveformView_.setWaveformData(std::move(waveform));
}

void DeckComponent::updateTimeLabels(double currentSec, double durationSec) {
  timeElapsedLabel_.setText(formatTime(currentSec), juce::dontSendNotification);
  const double remain = (durationSec > currentSec) ? (durationSec - currentSec) : 0.0;
  timeRemainingLabel_.setText("-" + formatTime(remain), juce::dontSendNotification);
}

void DeckComponent::updateTelemetry(const core::DeckTelemetry& telemetry) {
  telemetry_ = telemetry;
  waveformView_.updateTelemetry(telemetry);

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
  keyLabel_.setBounds(headerRight.removeFromTop(20));

  area.removeFromTop(4);

  // 2. Waveform View (Height ~100px)
  const int waveformH = std::max(70, area.getHeight() * 35 / 100);
  waveformView_.setBounds(area.removeFromTop(waveformH));

  area.removeFromTop(6);

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
  const int stemColW = stemsArea.getWidth() / kNumStems;
  for (int i = 0; i < kNumStems; ++i) {
    auto col = stemsArea.removeFromLeft(stemColW).reduced(2, 1);
    stemMuteButtons_[static_cast<std::size_t>(i)].setBounds(col.removeFromTop(20));
    stemVolumeSliders_[static_cast<std::size_t>(i)].setBounds(col);
  }

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
