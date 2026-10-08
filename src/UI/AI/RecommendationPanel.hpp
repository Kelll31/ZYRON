// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

#include "Core/AI/RecommendationTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/Ids.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Next-track recommendation panel UI component (SPEC section 52, ROADMAP P7-02).
class RecommendationPanel : public juce::Component, public juce::TableListBoxModel {
 public:
  using LoadCallback = std::function<void(core::DeckId deck, std::int64_t trackId)>;

  RecommendationPanel();
  explicit RecommendationPanel(core::ITrackRecommender& recommender);
  ~RecommendationPanel() override = default;

  void setTheme(const Theme& theme);
  void setRecommender(core::ITrackRecommender* recommender);
  void setCatalog(std::vector<core::TrackItem> catalog);
  void setCurrentTrack(const core::TrackItem& track);

  void setOnLoadTrack(LoadCallback cb) { onLoadTrack_ = std::move(cb); }

  void refreshRecommendations();

  // Component overrides
  void paint(juce::Graphics& g) override;
  void resized() override;

  // TableListBoxModel overrides
  int getNumRows() override;
  void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
  void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
  juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) override;

  [[nodiscard]] const std::vector<core::TrackRecommendation>& recommendations() const noexcept {
    return recommendations_;
  }

 private:
  core::ITrackRecommender* recommender_{nullptr};
  std::vector<core::TrackItem> catalog_;
  core::TrackItem currentTrack_;
  std::vector<core::TrackRecommendation> recommendations_;
  core::RecommendationFilter filter_;
  Theme theme_{Theme::dark()};
  LoadCallback onLoadTrack_;

  juce::Label headerLabel_;
  juce::Label currentTrackLabel_;
  juce::ToggleButton harmonicOnlyToggle_{"Harmonic Only (Camelot)"};
  juce::ComboBox energyGoalCombo_;
  juce::Slider pitchToleranceSlider_;
  juce::Label pitchToleranceLabel_;
  juce::TextButton refreshButton_{"Refresh"};
  juce::TableListBox table_;
};

}  // namespace zyron::ui
