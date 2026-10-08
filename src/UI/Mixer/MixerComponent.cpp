// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Mixer/MixerComponent.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::ui {

// --- LevelMeter ---

LevelMeter::LevelMeter(Theme theme) : theme_(theme) {}

void LevelMeter::setTheme(const Theme& theme) {
  theme_ = theme;
  repaint();
}

void LevelMeter::setLevels(float peakLeft, float peakRight) {
  peakLeft_ = std::clamp(peakLeft, 0.0f, 1.0f);
  peakRight_ = std::clamp(peakRight, 0.0f, 1.0f);
  repaint();
}

void LevelMeter::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);

  const auto b = getLocalBounds().toFloat();
  const float barW = (b.getWidth() - 3.0f) / 2.0f;
  const float h = b.getHeight();

  auto drawBar = [&](float x, float level) {
    // Background slot
    g.setColour(theme_.panel.darker(0.3f));
    g.fillRect(x, b.getY(), barW, h);

    if (level <= 0.001f) return;

    const float fillH = level * h;
    const float fillY = b.getBottom() - fillH;

    // Green segment (0.0 to 0.7)
    const float greenH = std::min(fillH, 0.7f * h);
    if (greenH > 0.0f) {
      g.setColour(theme_.meterGreen);
      g.fillRect(x, b.getBottom() - greenH, barW, greenH);
    }

    // Yellow segment (0.7 to 0.9)
    if (level > 0.7f) {
      const float yellowH = std::min(fillH - 0.7f * h, 0.2f * h);
      g.setColour(theme_.meterYellow);
      g.fillRect(x, b.getBottom() - 0.7f * h - yellowH, barW, yellowH);
    }

    // Red segment (> 0.9)
    if (level > 0.9f) {
      const float redH = fillH - 0.9f * h;
      g.setColour(theme_.meterRed);
      g.fillRect(x, fillY, barW, redH);
    }
  };

  drawBar(b.getX(), peakLeft_);
  drawBar(b.getX() + barW + 2.0f, peakRight_);
}

// --- MixerComponent ---

MixerComponent::MixerComponent(Theme theme)
    : theme_(theme), masterMeter_(theme) {
  setupMasterSection();

  setupChannel(channels_[0], core::DeckId::A, TRANS("CH") + " A", theme_.deckA);
  setupChannel(channels_[1], core::DeckId::B, TRANS("CH") + " B", theme_.deckB);
  setupChannel(channels_[2], core::DeckId::C, TRANS("CH") + " C", theme_.deckC);
  setupChannel(channels_[3], core::DeckId::D, TRANS("CH") + " D", theme_.deckD);

  setupCrossfaderSection();
  setLayoutMode(LayoutMode::TwoChannels);
  setTheme(theme);
}

void MixerComponent::setupChannel(ChannelControls& ch, core::DeckId deck, const juce::String& name,
                                  juce::Colour accent) {
  ch.label.setText(name, juce::dontSendNotification);
  ch.label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
  ch.label.setColour(juce::Label::textColourId, accent);
  ch.label.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(ch.label);

  auto configureKnob = [this](ParamSlider& knob, double min, double max, double defVal,
                             const juce::String& suffix) {
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 42, 14);
    knob.setTextValueSuffix(suffix);
    knob.setRange(min, max, 0.1);
    knob.setValue(defVal, juce::dontSendNotification);
    knob.setDefaultValue(defVal);
    knob.setColour(juce::Slider::rotarySliderFillColourId, theme_.accent);
    knob.setColour(juce::Slider::rotarySliderOutlineColourId, theme_.background);
    knob.setColour(juce::Slider::textBoxTextColourId, theme_.textDim);
    knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(knob);
  };

  configureKnob(ch.gainKnob, -24.0, 12.0, 0.0, " dB");
  ch.gainKnob.setParameterName(TRANS("Channel gain (dB)"));
  ch.gainKnob.onValueChange = [this, deck, &ch] {
    if (onGainChanged) onGainChanged(deck, static_cast<float>(ch.gainKnob.getValue()));
  };

  configureKnob(ch.highKnob, -60.0, 12.0, 0.0, " dB");
  ch.highKnob.setParameterName(TRANS("EQ high (dB)"));
  ch.highKnob.onValueChange = [this, deck, &ch] {
    if (onEqChanged) onEqChanged(deck, core::EqBand::High, static_cast<float>(ch.highKnob.getValue()));
  };

  configureKnob(ch.midKnob, -60.0, 12.0, 0.0, " dB");
  ch.midKnob.setParameterName(TRANS("EQ mid (dB)"));
  ch.midKnob.onValueChange = [this, deck, &ch] {
    if (onEqChanged) onEqChanged(deck, core::EqBand::Mid, static_cast<float>(ch.midKnob.getValue()));
  };

  configureKnob(ch.lowKnob, -60.0, 12.0, 0.0, " dB");
  ch.lowKnob.setParameterName(TRANS("EQ low (dB)"));
  ch.lowKnob.onValueChange = [this, deck, &ch] {
    if (onEqChanged) onEqChanged(deck, core::EqBand::Low, static_cast<float>(ch.lowKnob.getValue()));
  };

  configureKnob(ch.filterKnob, -1.0, 1.0, 0.0, "");
  ch.filterKnob.setParameterName(TRANS("DJ filter (-1 low-pass, +1 high-pass)"));
  ch.filterKnob.onValueChange = [this, deck, &ch] {
    if (onFilterChanged) onFilterChanged(deck, static_cast<float>(ch.filterKnob.getValue()));
  };

  ch.cueButton.setClickingTogglesState(true);
  ch.cueButton.setColour(juce::TextButton::buttonColourId, theme_.panel);
  ch.cueButton.setColour(juce::TextButton::buttonOnColourId, theme_.cueActive.withAlpha(0.4f));
  ch.cueButton.setColour(juce::TextButton::textColourOffId, theme_.textDim);
  ch.cueButton.setColour(juce::TextButton::textColourOnId, theme_.cueActive);
  ch.cueButton.onClick = [this, deck, &ch] {
    if (onCueChanged) onCueChanged(deck, ch.cueButton.getToggleState());
  };
  addAndMakeVisible(ch.cueButton);

  addAndMakeVisible(ch.meter);

  ch.volumeFader.setSliderStyle(juce::Slider::LinearVertical);
  ch.volumeFader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  ch.volumeFader.setRange(0.0, 1.0, 0.01);
  ch.volumeFader.setValue(1.0, juce::dontSendNotification);
  ch.volumeFader.setDefaultValue(1.0);
  ch.volumeFader.setParameterName(TRANS("Channel volume (0 to 1)"));
  ch.volumeFader.setColour(juce::Slider::thumbColourId, theme_.text);
  ch.volumeFader.setColour(juce::Slider::trackColourId, theme_.background);
  ch.volumeFader.onValueChange = [this, deck, &ch] {
    if (onVolumeChanged) onVolumeChanged(deck, static_cast<float>(ch.volumeFader.getValue()));
  };
  addAndMakeVisible(ch.volumeFader);

  // Crossfader Assign: Thru (1), Left (2), Right (3)
  ch.assignSelector.addItem("THRU", 1);
  ch.assignSelector.addItem("X-F L", 2);
  ch.assignSelector.addItem("X-F R", 3);
  // Default: A, C -> Left (2); B, D -> Right (3) per SPEC §20
  const int defaultId = (deck == core::DeckId::A || deck == core::DeckId::C) ? 2 : 3;
  ch.assignSelector.setSelectedId(defaultId, juce::dontSendNotification);
  ch.assignSelector.setColour(juce::ComboBox::backgroundColourId, theme_.panel);
  ch.assignSelector.setColour(juce::ComboBox::textColourId, theme_.textDim);
  ch.assignSelector.onChange = [this, deck, &ch] {
    if (onCrossfaderAssignChanged) {
      core::CrossfaderAssign assign = core::CrossfaderAssign::Thru;
      if (ch.assignSelector.getSelectedId() == 2) {
        assign = core::CrossfaderAssign::Left;
      } else if (ch.assignSelector.getSelectedId() == 3) {
        assign = core::CrossfaderAssign::Right;
      }
      onCrossfaderAssignChanged(deck, assign);
    }
  };
  addAndMakeVisible(ch.assignSelector);
}

void MixerComponent::setupMasterSection() {
  masterLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
  masterLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  masterLabel_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(masterLabel_);

  masterGainKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  masterGainKnob_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 42, 14);
  masterGainKnob_.setTextValueSuffix(" dB");
  masterGainKnob_.setRange(-60.0, 12.0, 0.1);
  masterGainKnob_.setValue(0.0, juce::dontSendNotification);
  masterGainKnob_.setDefaultValue(0.0);
  masterGainKnob_.setParameterName(TRANS("Master gain (dB)"));
  masterGainKnob_.setColour(juce::Slider::rotarySliderFillColourId, theme_.accent);
  masterGainKnob_.setColour(juce::Slider::rotarySliderOutlineColourId, theme_.background);
  masterGainKnob_.setColour(juce::Slider::textBoxTextColourId, theme_.textDim);
  masterGainKnob_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
  masterGainKnob_.onValueChange = [this] {
    if (onMasterGainChanged) onMasterGainChanged(static_cast<float>(masterGainKnob_.getValue()));
  };
  addAndMakeVisible(masterGainKnob_);

  addAndMakeVisible(masterMeter_);
}

void MixerComponent::setupCrossfaderSection() {
  crossfaderSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
  crossfaderSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  crossfaderSlider_.setRange(-1.0, 1.0, 0.01);
  crossfaderSlider_.setValue(0.0, juce::dontSendNotification);
  crossfaderSlider_.setDefaultValue(0.0);
  crossfaderSlider_.setParameterName(TRANS("Crossfader (-1 left, +1 right)"));
  crossfaderSlider_.setColour(juce::Slider::thumbColourId, theme_.text);
  crossfaderSlider_.setColour(juce::Slider::trackColourId, theme_.background);
  crossfaderSlider_.onValueChange = [this] {
    if (onCrossfaderChanged) onCrossfaderChanged(static_cast<float>(crossfaderSlider_.getValue()));
  };
  addAndMakeVisible(crossfaderSlider_);

  curveSelector_.addItem(TRANS("Constant Power"), 1);
  curveSelector_.addItem(TRANS("Linear"), 2);
  curveSelector_.addItem(TRANS("Cut"), 3);
  curveSelector_.setSelectedId(1, juce::dontSendNotification);
  curveSelector_.setColour(juce::ComboBox::backgroundColourId, theme_.panel);
  curveSelector_.setColour(juce::ComboBox::textColourId, theme_.textDim);
  curveSelector_.onChange = [this] {
    if (onCrossfaderCurveChanged) {
      core::CrossfaderCurve curve = core::CrossfaderCurve::ConstantPower;
      if (curveSelector_.getSelectedId() == 2) {
        curve = core::CrossfaderCurve::Linear;
      } else if (curveSelector_.getSelectedId() == 3) {
        curve = core::CrossfaderCurve::Cut;
      }
      onCrossfaderCurveChanged(curve);
    }
  };
  addAndMakeVisible(curveSelector_);
}

void MixerComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  masterMeter_.setTheme(theme);
  for (auto& ch : channels_) {
    ch.meter.setTheme(theme);
  }
  repaint();
}

void MixerComponent::setLayoutMode(LayoutMode mode) {
  layoutMode_ = mode;
  const bool showCD = (mode == LayoutMode::FourChannels);

  auto setVisibleCh = [showCD](ChannelControls& ch) {
    ch.label.setVisible(showCD);
    ch.gainKnob.setVisible(showCD);
    ch.highKnob.setVisible(showCD);
    ch.midKnob.setVisible(showCD);
    ch.lowKnob.setVisible(showCD);
    ch.filterKnob.setVisible(showCD);
    ch.cueButton.setVisible(showCD);
    ch.meter.setVisible(showCD);
    ch.volumeFader.setVisible(showCD);
    ch.assignSelector.setVisible(showCD);
  };

  setVisibleCh(channels_[2]);  // Deck C
  setVisibleCh(channels_[3]);  // Deck D

  resized();
  repaint();
}

void MixerComponent::updateMeters(float chAPeakL, float chAPeakR, float chBPeakL, float chBPeakR,
                                  float masterPeakL, float masterPeakR) {
  channels_[0].meter.setLevels(chAPeakL, chAPeakR);
  channels_[1].meter.setLevels(chBPeakL, chBPeakR);
  masterMeter_.setLevels(masterPeakL, masterPeakR);
}

void MixerComponent::updateMeters(const std::array<std::pair<float, float>, core::kDeckCount>& channelPeaks,
                                  float masterPeakL, float masterPeakR) {
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    channels_[i].meter.setLevels(channelPeaks[i].first, channelPeaks[i].second);
  }
  masterMeter_.setLevels(masterPeakL, masterPeakR);
}

void MixerComponent::syncFromState(const core::AppState& state) {
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    auto& ch = channels_[i];
    const core::DeckState& deck = state.decks[i];
    ch.gainKnob.showExternalValue(deck.gainDb);
    ch.lowKnob.showExternalValue(deck.eqDb[core::index(core::EqBand::Low)]);
    ch.midKnob.showExternalValue(deck.eqDb[core::index(core::EqBand::Mid)]);
    ch.highKnob.showExternalValue(deck.eqDb[core::index(core::EqBand::High)]);
    ch.volumeFader.showExternalValue(deck.volume);
    ch.filterKnob.showExternalValue(deck.filter);
    if (ch.cueButton.getToggleState() != state.mixer.cue[i] && !ch.cueButton.isMouseButtonDown()) {
      ch.cueButton.setToggleState(state.mixer.cue[i], juce::dontSendNotification);
    }
  }
  crossfaderSlider_.showExternalValue(state.mixer.crossfader);
  masterGainKnob_.showExternalValue(state.mixer.masterGainDb);
}

void MixerComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.panel);

  // Border outlining mixer module
  g.setColour(theme_.background);
  g.drawRect(getLocalBounds(), 1);

  // Column dividers
  g.setColour(theme_.panel.darker(0.2f));
  const int count = (layoutMode_ == LayoutMode::FourChannels) ? 4 : 2;
  const float chW = static_cast<float>(getWidth()) / static_cast<float>(count);

  for (int i = 1; i < count; ++i) {
    const int x = static_cast<int>(chW * static_cast<float>(i));
    g.drawVerticalLine(x, 60.0f, static_cast<float>(getHeight() - 60));
  }
}

void MixerComponent::resized() {
  auto area = getLocalBounds().reduced(6);

  // 1. Master Section at Top (Height 56px)
  auto masterArea = area.removeFromTop(56);
  masterLabel_.setBounds(masterArea.removeFromTop(14));
  const int masterW = masterArea.getWidth();
  masterGainKnob_.setBounds(masterArea.removeFromLeft(masterW / 2).reduced(4, 0));
  masterMeter_.setBounds(masterArea.reduced(8, 2));

  area.removeFromTop(4);

  // 2. Crossfader Section at Bottom (Height 44px)
  auto xfaderArea = area.removeFromBottom(44);
  curveSelector_.setBounds(xfaderArea.removeFromBottom(18).reduced(12, 1));
  crossfaderSlider_.setBounds(xfaderArea.reduced(8, 2));

  area.removeFromBottom(4);

  auto layoutChannel = [](ChannelControls& ch, juce::Rectangle<int> r) {
    ch.label.setBounds(r.removeFromTop(16));

    // Knobs stack
    const int knobH = std::min(44, r.getHeight() / 7);
    ch.gainKnob.setBounds(r.removeFromTop(knobH).reduced(3, 1));
    ch.highKnob.setBounds(r.removeFromTop(knobH).reduced(3, 1));
    ch.midKnob.setBounds(r.removeFromTop(knobH).reduced(3, 1));
    ch.lowKnob.setBounds(r.removeFromTop(knobH).reduced(3, 1));
    ch.filterKnob.setBounds(r.removeFromTop(knobH).reduced(3, 1));

    ch.cueButton.setBounds(r.removeFromTop(20).reduced(4, 1));
    r.removeFromTop(2);

    // Crossfader assign selector right above fader
    ch.assignSelector.setBounds(r.removeFromBottom(18).reduced(2, 1));
    r.removeFromBottom(2);

    // Bottom part of channel: Meter alongside Fader
    auto faderAndMeter = r;
    const int meterW = 10;
    ch.meter.setBounds(faderAndMeter.removeFromRight(meterW).reduced(1, 1));
    faderAndMeter.removeFromRight(2);
    ch.volumeFader.setBounds(faderAndMeter.reduced(1, 0));
  };

  if (layoutMode_ == LayoutMode::FourChannels) {
    // 4 channel layout: order A, C, B, D or A, B, C, D
    const int chW = area.getWidth() / 4;
    auto aArea = area.removeFromLeft(chW).reduced(2, 0);
    auto cArea = area.removeFromLeft(chW).reduced(2, 0);
    auto bArea = area.removeFromLeft(chW).reduced(2, 0);
    auto dArea = area.reduced(2, 0);

    layoutChannel(channels_[0], aArea);
    layoutChannel(channels_[2], cArea);
    layoutChannel(channels_[1], bArea);
    layoutChannel(channels_[3], dArea);
  } else {
    // 2 channel layout: CH A and CH B
    const int chW = area.getWidth() / 2;
    auto chAreaA = area.removeFromLeft(chW).reduced(2, 0);
    auto chAreaB = area.reduced(2, 0);

    layoutChannel(channels_[0], chAreaA);
    layoutChannel(channels_[1], chAreaB);
  }
}

}  // namespace zyron::ui
