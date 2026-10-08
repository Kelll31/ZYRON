// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "Core/AI/AutonomousDjTypes.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// The Automix queue: what the AI plans to play, in order, with its reasons (SPEC sections 55-58). The track that plays
/// now is highlighted, the next one marked; the points the mix will use (mix in, mix out, drop) are shown per track,
/// and tracks that have not started can be moved or removed. The Transition column shows how each track is mixed in;
/// clicking it (or right-clicking a row) lets the DJ choose another style.
class AutomixQueueComponent final : public juce::Component, public juce::TableListBoxModel {
 public:
  enum ColumnId { ColIndex = 1, ColTitle, ColArtist, ColBpm, ColKey, ColEnergy, ColFit, ColTransition, ColMixIn, ColMixOut, ColDrop, ColWhy };

  explicit AutomixQueueComponent(Theme theme = Theme::dark());
  ~AutomixQueueComponent() override;

  /// Shows a new snapshot (cheap when nothing changed).
  void update(const core::AutomixQueue& queue);
  void setTheme(const Theme& theme);

  /// The user moved (from -> to) or removed (index) an upcoming track.
  std::function<void(std::size_t from, std::size_t to)> onMoveRequested;
  std::function<void(std::size_t index)> onRemoveRequested;
  std::function<void()> onJumpToTransitionRequested;
  /// The DJ picked the transition INTO the track at `row` from the menu (nullopt: leave it to the mix profile).
  std::function<void(std::size_t row, std::optional<core::TransitionStyle> style)> onTransitionChosen;

  // TableListBoxModel
  int getNumRows() override;
  void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
  void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
  void cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;

  void resized() override;
  void paint(juce::Graphics& g) override;

 private:
  [[nodiscard]] bool isUpcoming(int row) const;
  void showTransitionMenu(int row);
  [[nodiscard]] static bool sameQueue(const core::AutomixQueue& a, const core::AutomixQueue& b);

  Theme theme_;
  core::AutomixQueue queue_;

  juce::Label summaryLabel_;
  juce::TextButton moveUpButton_{TRANS("Up")};
  juce::TextButton moveDownButton_{TRANS("Down")};
  juce::TextButton removeButton_{TRANS("Remove")};
  juce::TextButton jumpButton_{TRANS("Jump to transition")};
  juce::TableListBox table_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomixQueueComponent)
};

}  // namespace zyron::ui
