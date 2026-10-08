// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Fx/FxHitsBar.hpp"

#include "UI/Fx/FxModel.hpp"
#include "UI/Localization.hpp"

namespace zyron::ui {

FxHitsBar::FxHitsBar(Theme theme) : theme_(theme) {
  titleLabel_.setText(TRANS("FX HITS"), juce::dontSendNotification);
  titleLabel_.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
  addAndMakeVisible(titleLabel_);

  for (std::size_t i = 0; i < pads_.size(); ++i) {
    const auto type = static_cast<core::FxHitType>(i);
    auto& pad = pads_[i];
    pad.setButtonText(fxHitLabel(type));
    pad.setTooltip(TRANS("Plays the effect over the mix (follows the tempo of the playing deck)"));
    pad.onClick = [this, type] {
      if (onHit) onHit(type, level());
    };
    addAndMakeVisible(pad);
  }

  levelLabel_.setText(TRANS("LEVEL"), juce::dontSendNotification);
  levelLabel_.setFont(juce::FontOptions(10.0f));
  levelLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(levelLabel_);

  levelKnob_.setSliderStyle(juce::Slider::LinearHorizontal);
  levelKnob_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  levelKnob_.setRange(0.0, 1.0, 0.01);
  levelKnob_.setValue(0.6, juce::dontSendNotification);
  levelKnob_.setDefaultValue(0.6);
  levelKnob_.setParameterName(TRANS("FX hit level (0 to 1)"));
  addAndMakeVisible(levelKnob_);

  applyThemeColours();
}

void FxHitsBar::applyThemeColours() {
  titleLabel_.setColour(juce::Label::textColourId, theme_.accent);
  levelLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  for (auto& pad : pads_) {
    pad.setColour(juce::TextButton::buttonColourId, theme_.panel);
    pad.setColour(juce::TextButton::textColourOffId, theme_.text);
  }
  levelKnob_.setColour(juce::Slider::thumbColourId, theme_.accent);
  levelKnob_.setColour(juce::Slider::trackColourId, theme_.accent.withAlpha(0.5f));
  levelKnob_.setColour(juce::Slider::backgroundColourId, theme_.panel);
}

void FxHitsBar::setTheme(const Theme& theme) {
  theme_ = theme;
  applyThemeColours();
  repaint();
}

void FxHitsBar::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);
}

void FxHitsBar::resized() {
  auto area = getLocalBounds().reduced(0, 2);
  titleLabel_.setBounds(area.removeFromLeft(62));
  auto levelArea = area.removeFromRight(juce::jmin(190, area.getWidth() / 3));
  levelLabel_.setBounds(levelArea.removeFromLeft(48));
  levelKnob_.setBounds(levelArea.reduced(2, 0));
  area.removeFromRight(6);
  const int padWidth = area.getWidth() / static_cast<int>(pads_.size());
  for (auto& pad : pads_) {
    pad.setBounds(area.removeFromLeft(padWidth).reduced(2, 0));
  }
}

}  // namespace zyron::ui
