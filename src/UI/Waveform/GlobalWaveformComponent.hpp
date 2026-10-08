// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "Core/Audio/DeckTelemetry.hpp"
#include "Core/Audio/WaveformData.hpp"
#include "Core/State/Ids.hpp"
#include "UI/Theme.hpp"
#include "UI/Waveform/WaveformView.hpp"

namespace zyron::ui {

/// Stacked global overview waveform displaying Decks A/B (or A/B/C/D) synchronously (SPEC sections 24, 61, 62).
/// Allows visual phrase and beat alignment, drop/breakdown overview, and instant seek across all decks.
class GlobalWaveformComponent : public juce::Component {
 public:
  enum class LayoutMode { TwoDecks, FourDecks };

  explicit GlobalWaveformComponent(Theme theme = Theme::dark());
  ~GlobalWaveformComponent() override = default;

  void setTheme(const Theme& theme);
  void setLayoutMode(LayoutMode mode);
  [[nodiscard]] LayoutMode layoutMode() const noexcept { return layoutMode_; }

  void setWaveformData(core::DeckId deck, core::WaveformData data);
  void updateTelemetry(core::DeckId deck, const core::DeckTelemetry& telemetry);
  /// Automix transition regions of the track on `deck` (empty clears them).
  void setTransitionRegions(core::DeckId deck, const std::vector<core::TransitionRegion>& regions);

  // User seek callback across any deck
  std::function<void(core::DeckId deck, double seekSeconds)> onSeekRequested;

  void paint(juce::Graphics& g) override;
  void resized() override;

  [[nodiscard]] WaveformView& waveformView(core::DeckId deck);

 private:
  Theme theme_;
  LayoutMode layoutMode_{LayoutMode::TwoDecks};

  juce::Label badgeA_{"badgeA", "A"};
  juce::Label badgeB_{"badgeB", "B"};
  juce::Label badgeC_{"badgeC", "C"};
  juce::Label badgeD_{"badgeD", "D"};

  WaveformView waveformA_;
  WaveformView waveformB_;
  WaveformView waveformC_;
  WaveformView waveformD_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalWaveformComponent)
};

}  // namespace zyron::ui
