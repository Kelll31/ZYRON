// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "Core/State/Ids.hpp"
#include "UI/ParamSlider.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// A strip of six performance-hit pads (air horn, siren, riser, downlifter, impact, laser) and one level knob. A press
/// only reports "hit of this type at this level"; the owner turns it into a TriggerFxHit with the tempo of the playing deck.
class FxHitsBar final : public juce::Component {
 public:
  static constexpr int kPreferredHeight = 28;

  explicit FxHitsBar(Theme theme = Theme::dark());

  void setTheme(const Theme& theme);
  [[nodiscard]] float level() const noexcept { return static_cast<float>(levelKnob_.getValue()); }

  std::function<void(core::FxHitType type, float level)> onHit;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  void applyThemeColours();

  Theme theme_;
  juce::Label titleLabel_;
  std::array<juce::TextButton, core::kFxHitTypeCount> pads_;
  juce::Label levelLabel_;
  ParamSlider levelKnob_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxHitsBar)
};

}  // namespace zyron::ui
