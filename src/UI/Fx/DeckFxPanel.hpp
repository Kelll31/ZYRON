// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "Core/State/AppState.hpp"
#include "Core/State/Ids.hpp"
#include "UI/ParamSlider.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// The two effect slots of one deck's channel: type, on/off, WET and the type's main knob. Shown in a call-out from the
/// deck's FX button. It mirrors DeckState::fx (Automix and MIDI change them too) and reports user edits as whole slots;
/// knob drags are coalesced to ~30 per second so the Command bus is not flooded.
class DeckFxPanel final : public juce::Component, private juce::Timer {
 public:
  static constexpr int kWidth = 400;
  static constexpr int kHeight = 168;

  DeckFxPanel(core::DeckId deck, Theme theme);
  ~DeckFxPanel() override;

  [[nodiscard]] core::DeckId deckId() const noexcept { return deck_; }
  void syncFromState(const core::DeckState& state);

  std::function<void(core::DeckId deck, int slot, const core::FxSlotState& slot_state)> onSlotChanged;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  struct Slot {
    juce::Label title;
    juce::ComboBox typeBox;
    juce::TextButton onButton{"ON"};
    juce::Label wetLabel;
    ParamSlider wetKnob;
    juce::Label paramLabel;
    ParamSlider paramKnob;
    bool dirty{false};
  };

  void setupSlot(Slot& slot, int index);
  void updateParamLabel(Slot& slot, core::FxType type);
  [[nodiscard]] core::FxSlotState read(const Slot& slot) const;
  void flush(int index);
  void timerCallback() override;

  core::DeckId deck_;
  Theme theme_;
  std::array<Slot, core::kFxSlotCount> slots_;
  bool mirroring_{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeckFxPanel)
};

}  // namespace zyron::ui
