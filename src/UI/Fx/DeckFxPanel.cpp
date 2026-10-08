// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Fx/DeckFxPanel.hpp"

#include "UI/Fx/FxModel.hpp"
#include "UI/Localization.hpp"

namespace zyron::ui {

namespace {
constexpr int kTimerHz = 30;
constexpr int kRowHeight = 70;
}  // namespace

DeckFxPanel::DeckFxPanel(core::DeckId deck, Theme theme) : deck_(deck), theme_(theme) {
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    setupSlot(slots_[i], static_cast<int>(i));
  }
  setSize(kWidth, kHeight);
  startTimerHz(kTimerHz);
}

DeckFxPanel::~DeckFxPanel() {
  stopTimer();
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].dirty) {
      flush(static_cast<int>(i));  // a drag that ended after the last tick must not be lost
    }
  }
}

void DeckFxPanel::setupSlot(Slot& slot, int index) {
  slot.title.setText(juce::String(TRANS("FX")) + " " + juce::String(index + 1), juce::dontSendNotification);
  slot.title.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
  slot.title.setColour(juce::Label::textColourId, theme_.accent);
  addAndMakeVisible(slot.title);

  for (std::size_t t = 0; t < core::kFxTypeCount; ++t) {
    slot.typeBox.addItem(fxTypeLabel(static_cast<core::FxType>(t)), static_cast<int>(t) + 1);
  }
  slot.typeBox.setSelectedId(1, juce::dontSendNotification);
  slot.typeBox.setColour(juce::ComboBox::backgroundColourId, theme_.background);
  slot.typeBox.setColour(juce::ComboBox::textColourId, theme_.text);
  slot.typeBox.setColour(juce::ComboBox::outlineColourId, theme_.textDim.withAlpha(0.4f));
  slot.typeBox.setColour(juce::ComboBox::arrowColourId, theme_.textDim);
  slot.typeBox.setTooltip(TRANS("Effect type of this slot (None empties it)"));
  slot.typeBox.onChange = [this, &slot, index] {
    if (mirroring_) {
      return;
    }
    const bool none = slot.typeBox.getSelectedId() == 1;
    slot.onButton.setToggleState(!none, juce::dontSendNotification);  // picking an effect switches it on
    updateParamLabel(slot, static_cast<core::FxType>(slot.typeBox.getSelectedId() - 1));
    flush(index);
  };
  addAndMakeVisible(slot.typeBox);

  slot.onButton.setClickingTogglesState(true);
  slot.onButton.setColour(juce::TextButton::buttonColourId, theme_.panel);
  slot.onButton.setColour(juce::TextButton::buttonOnColourId, theme_.accent.withAlpha(0.35f));
  slot.onButton.setColour(juce::TextButton::textColourOffId, theme_.textDim);
  slot.onButton.setColour(juce::TextButton::textColourOnId, theme_.accent);
  slot.onButton.setTooltip(TRANS("Switches the effect on or off"));
  slot.onButton.onClick = [this, index] {
    if (!mirroring_) flush(index);
  };
  addAndMakeVisible(slot.onButton);

  const auto setupKnob = [this, &slot](ParamSlider& knob, juce::Label& label, const juce::String& name) {
    label.setText(name, juce::dontSendNotification);
    label.setFont(juce::FontOptions(10.0f));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, theme_.textDim);
    addAndMakeVisible(label);

    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 14);
    knob.setRange(0.0, 1.0, 0.01);
    knob.setValue(0.5, juce::dontSendNotification);
    knob.setDefaultValue(0.5);
    knob.setParameterName(name);
    knob.setColour(juce::Slider::rotarySliderFillColourId, theme_.accent);
    knob.setColour(juce::Slider::rotarySliderOutlineColourId, theme_.background);
    knob.setColour(juce::Slider::textBoxTextColourId, theme_.textDim);
    knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.onValueChange = [this, &slot] {
      if (!mirroring_) slot.dirty = true;  // sent by the next timer tick
    };
    addAndMakeVisible(knob);
  };
  setupKnob(slot.wetKnob, slot.wetLabel, TRANS("WET"));
  setupKnob(slot.paramKnob, slot.paramLabel, TRANS("PARAM"));
}

void DeckFxPanel::updateParamLabel(Slot& slot, core::FxType type) {
  slot.paramLabel.setText(fxParamLabel(type), juce::dontSendNotification);
  slot.paramKnob.setParameterName(fxParamLabel(type));
}

core::FxSlotState DeckFxPanel::read(const Slot& slot) const {
  core::FxSlotState state;
  state.type = static_cast<core::FxType>(juce::jmax(1, slot.typeBox.getSelectedId()) - 1);
  state.enabled = slot.onButton.getToggleState() && state.type != core::FxType::None;
  state.wet = static_cast<float>(slot.wetKnob.getValue());
  state.param = static_cast<float>(slot.paramKnob.getValue());
  state.tailAfterFader = true;  // echoes and reverb keep ringing when the channel fader closes
  return state;
}

void DeckFxPanel::flush(int index) {
  auto& slot = slots_[static_cast<std::size_t>(index)];
  slot.dirty = false;
  if (onSlotChanged) {
    onSlotChanged(deck_, index, read(slot));
  }
}

void DeckFxPanel::timerCallback() {
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].dirty) {
      flush(static_cast<int>(i));
    }
  }
}

void DeckFxPanel::syncFromState(const core::DeckState& state) {
  const juce::ScopedValueSetter<bool> quiet(mirroring_, true);
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    auto& slot = slots_[i];
    if (slot.dirty) {
      continue;  // the user's edit is on its way to the engine: do not snap back to the old state
    }
    const core::FxSlotState& fx = state.fx[i];
    if (!slot.typeBox.isPopupActive()) {
      slot.typeBox.setSelectedId(static_cast<int>(core::index(fx.type)) + 1, juce::dontSendNotification);
    }
    if (!slot.onButton.isMouseButtonDown()) {
      slot.onButton.setToggleState(fx.enabled, juce::dontSendNotification);
    }
    slot.wetKnob.showExternalValue(fx.wet);
    slot.paramKnob.showExternalValue(fx.param);
    updateParamLabel(slot, fx.type);
  }
}

void DeckFxPanel::paint(juce::Graphics& g) {
  g.fillAll(theme_.panel);
  g.setColour(theme_.background);
  g.drawHorizontalLine(kRowHeight + 12, 8.0f, static_cast<float>(getWidth() - 8));
}

void DeckFxPanel::resized() {
  auto area = getLocalBounds().reduced(10, 8);
  for (auto& slot : slots_) {
    auto row = area.removeFromTop(kRowHeight);
    auto left = row.removeFromLeft(150);
    slot.title.setBounds(left.removeFromTop(18));
    slot.typeBox.setBounds(left.removeFromTop(24));
    left.removeFromTop(4);
    slot.onButton.setBounds(left.removeFromTop(22).removeFromLeft(60));
    const int knobWidth = row.getWidth() / 2;
    auto wet = row.removeFromLeft(knobWidth);
    slot.wetLabel.setBounds(wet.removeFromTop(14));
    slot.wetKnob.setBounds(wet);
    slot.paramLabel.setBounds(row.removeFromTop(14));
    slot.paramKnob.setBounds(row);
    area.removeFromTop(6);
  }
}

}  // namespace zyron::ui
