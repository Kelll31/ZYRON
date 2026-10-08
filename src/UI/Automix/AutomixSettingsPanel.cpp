// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Automix/AutomixSettingsPanel.hpp"

#include <algorithm>
#include <cmath>

#include "UI/Automix/TransitionLabels.hpp"
#include "UI/Localization.hpp"

namespace zyron::ui {

namespace {

constexpr int kModeIdBase = 1;      // combo ids: Smooth 1 .. Custom 4
constexpr int kLengthIds[] = {16, 32, 64};  // the combo id is the length in beats
constexpr int kRowHeight = 24;

juce::String modeDescription(core::MixMode mode) {
  switch (mode) {
    case core::MixMode::Smooth: return TRANS("Smooth, like a streaming crossfade but on the beat");
    case core::MixMode::Club: return TRANS("Club: blends, filter sweeps, loop rolls and brakes");
    case core::MixMode::Battle: return TRANS("Hard: DJ battle, scratches and cuts on the drop");
    case core::MixMode::Custom: return TRANS("Your own choice of techniques");
  }
  return {};
}

}  // namespace

AutomixSettingsPanel::AutomixSettingsPanel(Theme theme) : theme_(theme) {
  titleLabel_.setText(TRANS("MIX STYLE"), juce::dontSendNotification);
  titleLabel_.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
  modeLabel_.setText(TRANS("Mode"), juce::dontSendNotification);
  lengthLabel_.setText(TRANS("Transition length"), juce::dontSendNotification);
  modeDescription_.setFont(juce::FontOptions(11.0f));
  modeDescription_.setMinimumHorizontalScale(1.0f);  // wrap instead of squeezing
  rotationLabel_.setFont(juce::FontOptions(11.0f));
  rotationLabel_.setMinimumHorizontalScale(1.0f);
  rotationLabel_.setJustificationType(juce::Justification::topLeft);
  modeDescription_.setJustificationType(juce::Justification::topLeft);

  modeCombo_.addItem(TRANS("Smooth"), kModeIdBase + static_cast<int>(core::MixMode::Smooth));
  modeCombo_.addItem(TRANS("Club"), kModeIdBase + static_cast<int>(core::MixMode::Club));
  modeCombo_.addItem(TRANS("Battle"), kModeIdBase + static_cast<int>(core::MixMode::Battle));
  modeCombo_.addItem(TRANS("Custom"), kModeIdBase + static_cast<int>(core::MixMode::Custom));
  modeCombo_.setTooltip(TRANS("Picks a ready-made mixing style; the techniques below follow it"));
  modeCombo_.onChange = [this] { modeChosen(); };

  const std::array<std::pair<juce::ToggleButton*, core::TransitionStyle>, 7> styled{{
      {&blendToggle_, core::TransitionStyle::BassSwap},
      {&filterToggle_, core::TransitionStyle::FilterFade},
      {&loopRollToggle_, core::TransitionStyle::LoopRoll},
      {&brakeToggle_, core::TransitionStyle::Brake},
      {&scratchToggle_, core::TransitionStyle::Scratch},
      {&cutToggle_, core::TransitionStyle::QuickCut},
      {&beatLoopToggle_, core::TransitionStyle::BeatLoopIn},
  }};
  for (const auto& [toggle, style] : styled) {
    toggle->setButtonText(transitionStyleLabel(style));
  }
  doubleDropToggle_.setButtonText(TRANS("Double drop"));
  stemsToggle_.setButtonText(TRANS("Stems: acapella blends"));
  fxOutToggle_.setButtonText(TRANS("Echo / reverb out"));
  fxHitsToggle_.setButtonText(TRANS("FX hits on drops"));
  stemsToggle_.setTooltip(TRANS("Separates the tracks into stems ahead of time and mixes with them (needs the stem model)"));
  fxHitsToggle_.setTooltip(TRANS("Synthesized air horns, impacts and risers on the drops"));
  for (auto* toggle : toggles()) {
    toggle->onClick = [this] { techniqueToggled(); };
    addAndMakeVisible(*toggle);
  }

  favouritesLabel_.setFont(juce::FontOptions(11.0f));
  favouritesLabel_.setMinimumHorizontalScale(1.0f);
  favouritesLabel_.setJustificationType(juce::Justification::topLeft);
  addAndMakeVisible(favouritesLabel_);
  forgetButton_.setTooltip(TRANS("Forget which transitions you picked by hand; the Automix stops favouring them"));
  forgetButton_.onClick = [this] {
    if (onTasteReset) onTasteReset();
  };
  addAndMakeVisible(forgetButton_);

  for (const int beats : kLengthIds) {
    lengthCombo_.addItem(juce::String(TRANS("%s beats")).replace("%s", juce::String(beats)), beats);
  }
  lengthCombo_.setTooltip(TRANS("How long a mix between two tracks lasts"));
  lengthCombo_.onChange = [this] { lengthChosen(); };

  for (auto* c : std::initializer_list<juce::Component*>{&titleLabel_, &modeLabel_, &modeCombo_, &modeDescription_,
                                                         &lengthLabel_, &lengthCombo_, &rotationLabel_}) {
    addAndMakeVisible(*c);
  }
  applyThemeColours();
  refreshControls();
}

void AutomixSettingsPanel::applyThemeColours() {
  for (auto* label : {&titleLabel_, &modeLabel_, &lengthLabel_}) {
    label->setColour(juce::Label::textColourId, label == &titleLabel_ ? theme_.accent : theme_.text);
  }
  for (auto* label : {&modeDescription_, &rotationLabel_, &favouritesLabel_}) {
    label->setColour(juce::Label::textColourId, theme_.textDim);
  }
  for (auto* combo : {&modeCombo_, &lengthCombo_}) {
    combo->setColour(juce::ComboBox::backgroundColourId, theme_.background);
    combo->setColour(juce::ComboBox::textColourId, theme_.text);
    combo->setColour(juce::ComboBox::outlineColourId, theme_.textDim.withAlpha(0.4f));
    combo->setColour(juce::ComboBox::arrowColourId, theme_.textDim);
  }
  forgetButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  forgetButton_.setColour(juce::TextButton::textColourOffId, theme_.accent);
  for (auto* toggle : toggles()) {
    toggle->setColour(juce::ToggleButton::textColourId, theme_.text);
    toggle->setColour(juce::ToggleButton::tickColourId, theme_.accent);
    toggle->setColour(juce::ToggleButton::tickDisabledColourId, theme_.textDim);
  }
}

std::array<juce::ToggleButton*, 11> AutomixSettingsPanel::toggles() {
  return {&blendToggle_,    &filterToggle_,   &beatLoopToggle_, &loopRollToggle_, &brakeToggle_,   &scratchToggle_,
          &cutToggle_,      &doubleDropToggle_, &stemsToggle_,  &fxOutToggle_,    &fxHitsToggle_};
}

void AutomixSettingsPanel::setTheme(const Theme& theme) {
  theme_ = theme;
  applyThemeColours();
  repaint();
}

void AutomixSettingsPanel::setProfile(const core::MixProfile& profile) {
  profile_ = profile;
  refreshControls();
}

void AutomixSettingsPanel::refreshControls() {
  const juce::ScopedValueSetter<bool> quiet(refreshing_, true);
  modeCombo_.setSelectedId(kModeIdBase + static_cast<int>(profile_.mode), juce::dontSendNotification);
  blendToggle_.setToggleState(profile_.blend, juce::dontSendNotification);
  filterToggle_.setToggleState(profile_.filter, juce::dontSendNotification);
  loopRollToggle_.setToggleState(profile_.loopRoll, juce::dontSendNotification);
  brakeToggle_.setToggleState(profile_.brake, juce::dontSendNotification);
  scratchToggle_.setToggleState(profile_.scratch, juce::dontSendNotification);
  beatLoopToggle_.setToggleState(profile_.beatLoop, juce::dontSendNotification);
  cutToggle_.setToggleState(profile_.cut, juce::dontSendNotification);
  doubleDropToggle_.setToggleState(profile_.doubleDrop, juce::dontSendNotification);
  stemsToggle_.setToggleState(profile_.stems, juce::dontSendNotification);
  fxOutToggle_.setToggleState(profile_.fxOut, juce::dontSendNotification);
  fxHitsToggle_.setToggleState(profile_.fxHits, juce::dontSendNotification);
  lengthCombo_.setSelectedId(static_cast<int>(std::lround(profile_.transitionBeats)), juce::dontSendNotification);
  modeDescription_.setText(modeDescription(profile_.mode), juce::dontSendNotification);

  // The order the Automix will use the techniques in: made by Core, so what is shown is what is played.
  juce::String order;
  for (const auto style : profile_.rotation()) {
    order << (order.isEmpty() ? "" : " " + juce::String::charToString(0x2192) + " ") << transitionStyleLabel(style);
  }
  rotationLabel_.setText(juce::String(TRANS("Order")) + ": " + order, juce::dontSendNotification);

  juce::String favourites;
  for (const auto style : profile_.favourites) {
    favourites << (favourites.isEmpty() ? "" : ", ") << transitionStyleLabel(style);
  }
  favouritesLabel_.setText(juce::String(TRANS("Your favourites")) + ": " +
                               (favourites.isEmpty() ? juce::String(TRANS("none yet (pick a transition by hand 3 times)"))
                                                     : favourites),
                           juce::dontSendNotification);
  forgetButton_.setEnabled(!profile_.favourites.empty());
}

void AutomixSettingsPanel::modeChosen() {
  if (refreshing_) {
    return;
  }
  const auto mode = static_cast<core::MixMode>(modeCombo_.getSelectedId() - kModeIdBase);
  const auto favourites = profile_.favourites;  // the DJ's taste is not part of a preset
  if (mode == core::MixMode::Custom) {
    profile_.mode = mode;  // keeps the ticked techniques and the length as they are
  } else {
    profile_ = core::MixProfile::preset(mode);
    profile_.favourites = favourites;
  }
  refreshControls();
  notifyChanged();
}

void AutomixSettingsPanel::techniqueToggled() {
  if (refreshing_) {
    return;
  }
  profile_.blend = blendToggle_.getToggleState();
  profile_.filter = filterToggle_.getToggleState();
  profile_.loopRoll = loopRollToggle_.getToggleState();
  profile_.brake = brakeToggle_.getToggleState();
  profile_.scratch = scratchToggle_.getToggleState();
  profile_.beatLoop = beatLoopToggle_.getToggleState();
  profile_.cut = cutToggle_.getToggleState();
  profile_.doubleDrop = doubleDropToggle_.getToggleState();
  profile_.stems = stemsToggle_.getToggleState();
  profile_.fxOut = fxOutToggle_.getToggleState();
  profile_.fxHits = fxHitsToggle_.getToggleState();
  profile_.mode = core::MixMode::Custom;
  refreshControls();
  notifyChanged();
}

void AutomixSettingsPanel::lengthChosen() {
  if (refreshing_ || lengthCombo_.getSelectedId() <= 0) {
    return;
  }
  profile_.transitionBeats = static_cast<double>(lengthCombo_.getSelectedId());
  profile_.mode = core::MixMode::Custom;
  refreshControls();
  notifyChanged();
}

void AutomixSettingsPanel::notifyChanged() {
  if (onProfileChanged) {
    onProfileChanged(profile_);
  }
}

void AutomixSettingsPanel::paint(juce::Graphics& g) {
  g.fillAll(theme_.panel);
}

void AutomixSettingsPanel::resized() {
  auto area = getLocalBounds().reduced(10, 8);
  titleLabel_.setBounds(area.removeFromTop(kRowHeight));
  area.removeFromTop(4);
  modeLabel_.setBounds(area.removeFromTop(kRowHeight));
  modeCombo_.setBounds(area.removeFromTop(kRowHeight + 2));
  modeDescription_.setBounds(area.removeFromTop(34));
  area.removeFromTop(4);
  // Two columns of six, filled row by row; the host scrolls the panel when the bottom area is too short for it.
  const auto all = toggles();
  constexpr int kToggleRows = (11 + 1) / 2;
  auto toggleArea = area.removeFromTop(kRowHeight * kToggleRows);
  const int columnWidth = toggleArea.getWidth() / 2;
  for (std::size_t i = 0; i < all.size(); ++i) {
    const int column = static_cast<int>(i % 2);
    const int row = static_cast<int>(i / 2);
    all[i]->setBounds(toggleArea.getX() + column * columnWidth, toggleArea.getY() + row * kRowHeight, columnWidth,
                      kRowHeight);
  }
  area.removeFromTop(6);
  lengthLabel_.setBounds(area.removeFromTop(kRowHeight));
  lengthCombo_.setBounds(area.removeFromTop(kRowHeight + 2));
  area.removeFromTop(8);
  rotationLabel_.setBounds(area.removeFromTop(std::min(area.getHeight(), 48)));
  area.removeFromTop(4);
  auto favouritesArea = area.removeFromTop(std::min(area.getHeight(), 40));
  forgetButton_.setBounds(favouritesArea.removeFromRight(70).removeFromTop(kRowHeight));
  favouritesArea.removeFromRight(4);
  favouritesLabel_.setBounds(favouritesArea);
}

}  // namespace zyron::ui
