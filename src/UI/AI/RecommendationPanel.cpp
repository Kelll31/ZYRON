// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/AI/RecommendationPanel.hpp"

#include <iomanip>
#include <sstream>

namespace zyron::ui {

namespace {

enum ColumnId {
  ColRank = 1,
  ColScore,
  ColArtist,
  ColTitle,
  ColBpm,
  ColKey,
  ColEnergy,
  ColReason,
  ColActions
};

class ActionButtonsCell : public juce::Component {
 public:
  ActionButtonsCell(std::int64_t trackId, RecommendationPanel::LoadCallback onLoad)
      : trackId_(trackId), onLoad_(std::move(onLoad)) {
    setupButton(btnA_, "A", core::DeckId::A);
    setupButton(btnB_, "B", core::DeckId::B);
    setupButton(btnC_, "C", core::DeckId::C);
    setupButton(btnD_, "D", core::DeckId::D);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(1);
    const int w = area.getWidth() / 4;
    btnA_.setBounds(area.removeFromLeft(w).reduced(1));
    btnB_.setBounds(area.removeFromLeft(w).reduced(1));
    btnC_.setBounds(area.removeFromLeft(w).reduced(1));
    btnD_.setBounds(area.reduced(1));
  }

 private:
  void setupButton(juce::TextButton& btn, const juce::String& text, core::DeckId deck) {
    btn.setButtonText(text);
    btn.onClick = [this, deck] {
      if (onLoad_) onLoad_(deck, trackId_);
    };
    addAndMakeVisible(btn);
  }

  std::int64_t trackId_{0};
  RecommendationPanel::LoadCallback onLoad_;
  juce::TextButton btnA_, btnB_, btnC_, btnD_;
};

}  // namespace

RecommendationPanel::RecommendationPanel()
    : RecommendationPanel(*static_cast<core::ITrackRecommender*>(nullptr)) {}

RecommendationPanel::RecommendationPanel(core::ITrackRecommender& recommender)
    : recommender_(&recommender) {
  // Header
  headerLabel_.setText("AI NEXT-TRACK RECOMMENDATIONS", juce::dontSendNotification);
  headerLabel_.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
  headerLabel_.setColour(juce::Label::textColourId, theme_.accent);
  addAndMakeVisible(headerLabel_);

  currentTrackLabel_.setText("Currently playing: None", juce::dontSendNotification);
  currentTrackLabel_.setFont(juce::FontOptions(12.0f));
  currentTrackLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(currentTrackLabel_);

  // Filters
  harmonicOnlyToggle_.setColour(juce::ToggleButton::textColourId, theme_.text);
  harmonicOnlyToggle_.onClick = [this] {
    filter_.harmonicOnly = harmonicOnlyToggle_.getToggleState();
    refreshRecommendations();
  };
  addAndMakeVisible(harmonicOnlyToggle_);

  energyGoalCombo_.addItem("Maintain Energy", 1);
  energyGoalCombo_.addItem("Build Energy (+)", 2);
  energyGoalCombo_.addItem("Cool Down (-)", 3);
  energyGoalCombo_.addItem("Any Energy", 4);
  energyGoalCombo_.setSelectedId(1, juce::dontSendNotification);
  energyGoalCombo_.onChange = [this] {
    switch (energyGoalCombo_.getSelectedId()) {
      case 1: filter_.energyGoal = core::EnergyGoal::Maintain; break;
      case 2: filter_.energyGoal = core::EnergyGoal::BuildUp; break;
      case 3: filter_.energyGoal = core::EnergyGoal::CoolDown; break;
      case 4: filter_.energyGoal = core::EnergyGoal::Any; break;
    }
    refreshRecommendations();
  };
  addAndMakeVisible(energyGoalCombo_);

  pitchToleranceSlider_.setRange(4.0, 16.0, 1.0);
  pitchToleranceSlider_.setValue(8.0, juce::dontSendNotification);
  pitchToleranceSlider_.setTextValueSuffix(" %");
  pitchToleranceSlider_.onValueChange = [this] {
    filter_.maxPitchBendPercent = static_cast<float>(pitchToleranceSlider_.getValue());
    refreshRecommendations();
  };
  addAndMakeVisible(pitchToleranceSlider_);

  pitchToleranceLabel_.setText("Max Tempo Shift:", juce::dontSendNotification);
  pitchToleranceLabel_.setFont(juce::FontOptions(11.0f));
  pitchToleranceLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(pitchToleranceLabel_);

  refreshButton_.onClick = [this] { refreshRecommendations(); };
  refreshButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  refreshButton_.setColour(juce::TextButton::textColourOffId, theme_.accent);
  addAndMakeVisible(refreshButton_);

  // Table
  auto& header = table_.getHeader();
  header.addColumn("#", ColRank, 32, 25, 45);
  header.addColumn("Match", ColScore, 60, 50, 80);
  header.addColumn("Artist", ColArtist, 120, 80, 200);
  header.addColumn("Title", ColTitle, 160, 100, 300);
  header.addColumn("BPM", ColBpm, 55, 45, 75);
  header.addColumn("Key", ColKey, 50, 40, 70);
  header.addColumn("Energy", ColEnergy, 55, 45, 75);
  header.addColumn("Transition Rationale", ColReason, 240, 120, 500);
  header.addColumn("Load to Deck", ColActions, 130, 100, 160);

  table_.setModel(this);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  table_.setOutlineThickness(0);
  addAndMakeVisible(table_);
}

void RecommendationPanel::setTheme(const Theme& theme) {
  theme_ = theme;
  headerLabel_.setColour(juce::Label::textColourId, theme_.accent);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  repaint();
}

void RecommendationPanel::setRecommender(core::ITrackRecommender* recommender) {
  recommender_ = recommender;
  refreshRecommendations();
}

void RecommendationPanel::setCatalog(std::vector<core::TrackItem> catalog) {
  catalog_ = std::move(catalog);
  refreshRecommendations();
}

void RecommendationPanel::setCurrentTrack(const core::TrackItem& track) {
  currentTrack_ = track;
  std::ostringstream ss;
  ss << "Currently playing: " << (track.artist.empty() ? "Unknown" : track.artist) << " - "
     << (track.title.empty() ? "Untitled" : track.title) << " ["
     << std::fixed << std::setprecision(1) << track.bpm << " BPM, "
     << (track.key.empty() ? "?" : track.key) << ", Energy "
     << std::fixed << std::setprecision(1) << track.energy << "]";
  currentTrackLabel_.setText(ss.str(), juce::dontSendNotification);
  refreshRecommendations();
}

void RecommendationPanel::refreshRecommendations() {
  if (!recommender_ || catalog_.empty()) {
    recommendations_.clear();
    table_.updateContent();
    return;
  }

  recommendations_ = recommender_->recommendNextTracks(currentTrack_, catalog_, filter_);
  table_.updateContent();
  table_.repaint();
}

void RecommendationPanel::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);
  g.setColour(theme_.panel);
  g.fillRect(getLocalBounds().removeFromTop(75));
}

void RecommendationPanel::resized() {
  auto area = getLocalBounds().reduced(8);

  // Top header row
  auto topRow = area.removeFromTop(24);
  headerLabel_.setBounds(topRow.removeFromLeft(260));
  refreshButton_.setBounds(topRow.removeFromRight(80));

  // Current track status row
  currentTrackLabel_.setBounds(area.removeFromTop(20));

  // Filters row
  auto filterRow = area.removeFromTop(26);
  harmonicOnlyToggle_.setBounds(filterRow.removeFromLeft(170));
  energyGoalCombo_.setBounds(filterRow.removeFromLeft(150));
  filterRow.removeFromLeft(15);
  pitchToleranceLabel_.setBounds(filterRow.removeFromLeft(105));
  pitchToleranceSlider_.setBounds(filterRow.removeFromLeft(120));

  area.removeFromTop(6);
  table_.setBounds(area);
}

int RecommendationPanel::getNumRows() {
  return static_cast<int>(recommendations_.size());
}

void RecommendationPanel::paintRowBackground(
    juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected) {
  if (rowIsSelected) {
    g.fillAll(theme_.panel.brighter(0.15f));
  } else if (rowNumber % 2 == 1) {
    g.fillAll(theme_.background.brighter(0.02f));
  }
}

void RecommendationPanel::paintCell(
    juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/) {
  if (rowNumber < 0 || rowNumber >= static_cast<int>(recommendations_.size())) {
    return;
  }

  const auto& rec = recommendations_[static_cast<std::size_t>(rowNumber)];
  g.setFont(juce::FontOptions(12.0f));

  auto cellArea = juce::Rectangle<int>(4, 0, width - 8, height);

  switch (columnId) {
    case ColRank:
      g.setColour(theme_.textDim);
      g.drawText(juce::String(rec.rank), cellArea, juce::Justification::centredLeft);
      break;

    case ColScore: {
      const int pct = static_cast<int>(std::round(rec.compatibility.overallScore * 100.0f));
      const auto scoreCol = (pct >= 85) ? theme_.accent : ((pct >= 70) ? theme_.meterYellow : theme_.textDim);
      g.setColour(scoreCol);
      g.drawText(juce::String(pct) + "%", cellArea, juce::Justification::centredLeft);
      break;
    }

    case ColArtist:
      g.setColour(theme_.text);
      g.drawText(rec.track.artist, cellArea, juce::Justification::centredLeft, true);
      break;

    case ColTitle:
      g.setColour(theme_.text);
      g.drawText(rec.track.title, cellArea, juce::Justification::centredLeft, true);
      break;

    case ColBpm:
      g.setColour(rec.compatibility.isBpmCompatible ? theme_.text : theme_.meterRed);
      g.drawText(juce::String(rec.track.bpm, 1), cellArea, juce::Justification::centredLeft);
      break;

    case ColKey:
      g.setColour(rec.compatibility.isHarmonic ? theme_.accent : theme_.text);
      g.drawText(rec.track.key, cellArea, juce::Justification::centredLeft);
      break;

    case ColEnergy:
      g.setColour(theme_.meterYellow);
      g.drawText(juce::String(rec.track.energy, 1), cellArea, juce::Justification::centredLeft);
      break;

    case ColReason:
      g.setColour(theme_.textDim);
      g.drawText(rec.compatibility.explanation, cellArea, juce::Justification::centredLeft, true);
      break;
  }
}

juce::Component* RecommendationPanel::refreshComponentForCell(
    int rowNumber, int columnId, bool /*isRowSelected*/, juce::Component* existingComponentToUpdate) {
  if (columnId != ColActions) {
    delete existingComponentToUpdate;
    return nullptr;
  }

  if (rowNumber < 0 || rowNumber >= static_cast<int>(recommendations_.size())) {
    delete existingComponentToUpdate;
    return nullptr;
  }

  const auto& rec = recommendations_[static_cast<std::size_t>(rowNumber)];

  auto* cell = dynamic_cast<ActionButtonsCell*>(existingComponentToUpdate);
  if (!cell) {
    cell = new ActionButtonsCell(rec.track.id, onLoadTrack_);
  }
  return cell;
}

}  // namespace zyron::ui
