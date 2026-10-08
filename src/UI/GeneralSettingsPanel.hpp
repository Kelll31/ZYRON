// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "UI/Localization.hpp"
#include "UI/Settings/Preferences.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// The "General" settings tab: the interface language (English or Russian) and the sound options (automatic loudness
/// of new tracks, master glue compressor and limiter). The language is applied at once, saved for the next start, and
/// the owner rebuilds the window so every text is read again; the other choices are reported through
/// onPreferencesChanged (the owner saves and applies them).
class GeneralSettingsPanel final : public juce::Component {
 public:
  explicit GeneralSettingsPanel(const Theme& theme, Preferences preferences = {})
      : theme_(theme), preferences_(preferences) {
    languageLabel_.setText(TRANS("Language"), juce::dontSendNotification);
    languageLabel_.setColour(juce::Label::textColourId, theme_.text);
    addAndMakeVisible(languageLabel_);

    // Language names stay in their own language so they can be found whatever the current one is.
    languageBox_.addItem("English", 1);
    languageBox_.addItem(juce::String::fromUTF8("\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"), 2);
    languageBox_.setSelectedId(i18n::current() == i18n::Language::Russian ? 2 : 1, juce::dontSendNotification);
    languageBox_.onChange = [this] {
      const auto language = languageBox_.getSelectedId() == 2 ? i18n::Language::Russian : i18n::Language::English;
      if (language == i18n::current()) {
        return;
      }
      i18n::apply(language);
      i18n::save(language);
      if (onLanguageChanged) onLanguageChanged();
    };
    addAndMakeVisible(languageBox_);

    setupToggle(autoLoudnessToggle_, TRANS("Auto loudness"), preferences_.autoLoudness,
                TRANS("Levels the loudness of every track you load to a common target (needs the track to be analysed)"),
                [](Preferences& p, bool on) { p.autoLoudness = on; });
    setupToggle(autoStemsToggle_, TRANS("Prepare stems on load"), preferences_.autoStems,
                TRANS("Separates every track you load into vocals, drums, bass and other in the background, so the stem "
                      "controls work right away (uses the CPU for about a minute per track)"),
                [](Preferences& p, bool on) { p.autoStems = on; });
    setupToggle(glueToggle_, TRANS("Master glue compressor"), preferences_.glue,
                TRANS("Gentle compression of the whole mix, like a hardware bus compressor"),
                [](Preferences& p, bool on) { p.glue = on; });
    setupToggle(limiterToggle_, TRANS("Master limiter"), preferences_.limiter,
                TRANS("Stops the output from clipping; keep it on unless something after ZYRON limits"),
                [](Preferences& p, bool on) { p.limiter = on; });
  }

  std::function<void()> onLanguageChanged;
  std::function<void(const Preferences&)> onPreferencesChanged;

  void paint(juce::Graphics& g) override { g.fillAll(theme_.panel); }

  void resized() override {
    auto area = getLocalBounds().reduced(16);
    auto row = area.removeFromTop(28);
    languageLabel_.setBounds(row.removeFromLeft(160));
    languageBox_.setBounds(row.removeFromLeft(220));
    area.removeFromTop(12);
    for (auto* toggle : {&autoLoudnessToggle_, &autoStemsToggle_, &glueToggle_, &limiterToggle_}) {
      toggle->setBounds(area.removeFromTop(28).removeFromLeft(380));
    }
  }

 private:
  template <typename Apply>
  void setupToggle(juce::ToggleButton& toggle, const juce::String& text, bool on, const juce::String& tip,
                   Apply apply) {
    toggle.setButtonText(text);
    toggle.setTooltip(tip);
    toggle.setToggleState(on, juce::dontSendNotification);
    toggle.setColour(juce::ToggleButton::textColourId, theme_.text);
    toggle.setColour(juce::ToggleButton::tickColourId, theme_.accent);
    toggle.onClick = [this, &toggle, apply] {
      apply(preferences_, toggle.getToggleState());
      if (onPreferencesChanged) onPreferencesChanged(preferences_);
    };
    addAndMakeVisible(toggle);
  }

  const Theme theme_;
  Preferences preferences_;
  juce::ToggleButton autoLoudnessToggle_;
  juce::ToggleButton autoStemsToggle_;
  juce::ToggleButton glueToggle_;
  juce::ToggleButton limiterToggle_;
  juce::Label languageLabel_;
  juce::ComboBox languageBox_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneralSettingsPanel)
};

}  // namespace zyron::ui
