// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

#include "Core/AI/SetBuilderTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/Ids.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Energy curve chart visualization component (SPEC section 56, ROADMAP P7-03).
class EnergyCurveChart : public juce::Component {
 public:
  EnergyCurveChart() = default;
  ~EnergyCurveChart() override = default;

  void setTheme(const Theme& theme) { theme_ = theme; repaint(); }
  void setPlan(const core::SetPlan& plan) { plan_ = plan; repaint(); }

  void paint(juce::Graphics& g) override;

 private:
  core::SetPlan plan_;
  Theme theme_{Theme::dark()};
};

/// Autonomous DJ set builder and energy curve planner UI component (SPEC sections 55, 56, ROADMAP P7-03).
class SetBuilderComponent : public juce::Component, public juce::TableListBoxModel {
 public:
  using LoadCallback = std::function<void(core::DeckId deck, std::int64_t trackId)>;

  SetBuilderComponent();
  explicit SetBuilderComponent(core::ISetBuilder& builder);
  ~SetBuilderComponent() override = default;

  void setTheme(const Theme& theme);
  void setBuilder(core::ISetBuilder* builder);
  void setCatalog(std::vector<core::TrackItem> catalog);

  void setOnLoadTrack(LoadCallback cb) { onLoadTrack_ = std::move(cb); }

  void planSet();

  // Component overrides
  void paint(juce::Graphics& g) override;
  void resized() override;

  // TableListBoxModel overrides
  int getNumRows() override;
  void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
  void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;

  [[nodiscard]] const core::SetPlan& currentPlan() const noexcept { return plan_; }

 private:
  core::ISetBuilder* builder_{nullptr};
  std::vector<core::TrackItem> catalog_;
  core::SetPlan plan_;
  Theme theme_{Theme::dark()};
  LoadCallback onLoadTrack_;

  juce::Label titleLabel_;
  juce::Label presetLabel_;
  juce::ComboBox presetCombo_;
  juce::Label durationLabel_;
  juce::ComboBox durationCombo_;
  juce::Label genreLabel_;
  juce::TextEditor genreEditor_;
  juce::TextButton buildButton_{TRANS("Generate 60-Min Set")};
  juce::Label summaryLabel_;

  EnergyCurveChart chart_;
  juce::TableListBox trackTable_;
};

}  // namespace zyron::ui
