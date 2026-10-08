// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "Core/AI/AutonomousDjTypes.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// How the Automix mixes: a mode (Smooth / Club / Battle / Custom), the techniques it may use and how long a transition
/// is. Picking a mode loads its preset into the controls; touching a technique or the length switches to Custom. The
/// panel only edits a core::MixProfile: the rotation shown is the one Core computes (SPEC sections 55-58).
class AutomixSettingsPanel final : public juce::Component {
 public:
  /// Height the controls need; the host scrolls the panel when it gets less.
  static constexpr int kPreferredHeight = 470;

  explicit AutomixSettingsPanel(Theme theme = Theme::dark());

  /// Shows a profile without reporting it back (used for the one loaded from disk).
  void setProfile(const core::MixProfile& profile);
  [[nodiscard]] const core::MixProfile& profile() const noexcept { return profile_; }
  void setTheme(const Theme& theme);

  /// The user changed something: apply the profile (and remember it).
  std::function<void(const core::MixProfile&)> onProfileChanged;
  /// The user pressed "Forget": the owner clears the counted choices and the favourites, then shows the new profile.
  std::function<void()> onTasteReset;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  void refreshControls();
  void modeChosen();
  void techniqueToggled();
  void lengthChosen();
  void notifyChanged();
  void applyThemeColours();
  [[nodiscard]] std::array<juce::ToggleButton*, 11> toggles();

  Theme theme_;
  core::MixProfile profile_{core::MixProfile::preset(core::MixMode::Club)};
  bool refreshing_{false};  // the controls are being set from the profile: their callbacks must stay quiet

  juce::Label titleLabel_;
  juce::Label modeLabel_;
  juce::ComboBox modeCombo_;
  juce::Label modeDescription_;
  juce::ToggleButton blendToggle_;
  juce::ToggleButton filterToggle_;
  juce::ToggleButton loopRollToggle_;
  juce::ToggleButton brakeToggle_;
  juce::ToggleButton scratchToggle_;
  juce::ToggleButton beatLoopToggle_;
  juce::ToggleButton cutToggle_;
  juce::ToggleButton doubleDropToggle_;
  juce::ToggleButton stemsToggle_;
  juce::ToggleButton fxOutToggle_;
  juce::ToggleButton fxHitsToggle_;
  juce::Label lengthLabel_;
  juce::ComboBox lengthCombo_;
  juce::Label rotationLabel_;
  juce::Label favouritesLabel_;
  juce::TextButton forgetButton_{TRANS("Forget")};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomixSettingsPanel)
};

}  // namespace zyron::ui
