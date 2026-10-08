// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/AI/SetBuilderComponent.hpp"
#include "UI/Localization.hpp"

#include <iomanip>
#include <sstream>

namespace zyron::ui {

namespace {

enum ColumnId {
  ColIndex = 1,
  ColTime,
  ColArtist,
  ColTitle,
  ColBpm,
  ColKey,
  ColEnergy,
  ColTransition
};

}  // namespace

void EnergyCurveChart::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat();
  g.fillAll(theme_.panel);

  // Chart margins
  const float marginLeft = 35.0f;
  const float marginRight = 15.0f;
  const float marginTop = 20.0f;
  const float marginBottom = 25.0f;

  const auto plotArea = juce::Rectangle<float>(
      marginLeft, marginTop,
      bounds.getWidth() - (marginLeft + marginRight),
      bounds.getHeight() - (marginTop + marginBottom));

  if (plotArea.getWidth() <= 10.0f || plotArea.getHeight() <= 10.0f) {
    return;
  }

  // Draw horizontal energy guide lines (2.0 .. 10.0)
  g.setFont(juce::FontOptions(10.0f));
  for (int e = 2; e <= 10; e += 2) {
    const float y = plotArea.getBottom() - plotArea.getHeight() * (static_cast<float>(e - 1) / 9.0f);
    g.setColour(theme_.panel.brighter(0.08f));
    g.drawHorizontalLine(static_cast<int>(y), plotArea.getX(), plotArea.getRight());

    g.setColour(theme_.textDim);
    g.drawText(juce::String(e), 2, static_cast<int>(y) - 6, static_cast<int>(marginLeft) - 6, 12,
               juce::Justification::centredRight);
  }

  // Legend
  g.setColour(theme_.deckA);
  g.drawText(TRANS("--- Target Energy"), static_cast<int>(plotArea.getX()), 4, 120, 14, juce::Justification::centredLeft);
  g.setColour(theme_.accent);
  g.drawText(TRANS("--- Realized Energy"), static_cast<int>(plotArea.getX()) + 130, 4, 130, 14, juce::Justification::centredLeft);

  if (!plan_.targetEnergyCurve.empty()) {
    // 1. Draw Target Curve (Cyan)
    juce::Path targetPath;
    const auto& targetCurve = plan_.targetEnergyCurve;
    const float stepX = plotArea.getWidth() / static_cast<float>(targetCurve.size() - 1);

    for (std::size_t i = 0; i < targetCurve.size(); ++i) {
      const float x = plotArea.getX() + static_cast<float>(i) * stepX;
      const float y = plotArea.getBottom() - plotArea.getHeight() * ((targetCurve[i] - 1.0f) / 9.0f);
      if (i == 0) {
        targetPath.startNewSubPath(x, y);
      } else {
        targetPath.lineTo(x, y);
      }
    }

    g.setColour(theme_.deckA);
    g.strokePath(targetPath, juce::PathStrokeType(2.0f));

    // 2. Draw Realized Curve (Accent Green)
    if (!plan_.realizedEnergyCurve.empty()) {
      juce::Path realPath;
      const auto& realCurve = plan_.realizedEnergyCurve;
      const float realStepX = plotArea.getWidth() / static_cast<float>(realCurve.size() - 1);

      for (std::size_t i = 0; i < realCurve.size(); ++i) {
        const float x = plotArea.getX() + static_cast<float>(i) * realStepX;
        const float y = plotArea.getBottom() - plotArea.getHeight() * ((realCurve[i] - 1.0f) / 9.0f);
        if (i == 0) {
          realPath.startNewSubPath(x, y);
        } else {
          realPath.lineTo(x, y);
        }
      }

      g.setColour(theme_.accent);
      g.strokePath(realPath, juce::PathStrokeType(2.5f));
    }

    // 3. Draw Track Boundary Markers
    for (const auto& entry : plan_.tracks) {
      if (plan_.totalDurationMinutes <= 0.0) break;
      const float trackX = plotArea.getX() + plotArea.getWidth() *
                                                 static_cast<float>(entry.startTimeMinutes / plan_.totalDurationMinutes);
      g.setColour(juce::Colour{0x55ffffff});
      g.drawVerticalLine(static_cast<int>(trackX), plotArea.getY(), plotArea.getBottom());

      g.setColour(theme_.textDim);
      g.drawText(juce::String(entry.trackIndex), static_cast<int>(trackX) + 2,
                 static_cast<int>(plotArea.getBottom()) + 2, 20, 14, juce::Justification::centredLeft);
    }
  }

  // Border
  g.setColour(theme_.panel.brighter(0.2f));
  g.drawRect(plotArea);
}

SetBuilderComponent::SetBuilderComponent()
    : SetBuilderComponent(*static_cast<core::ISetBuilder*>(nullptr)) {}

SetBuilderComponent::SetBuilderComponent(core::ISetBuilder& builder)
    : builder_(&builder) {
  // Title
  titleLabel_.setText(TRANS("AI SET BUILDER & ENERGY CURVE PLANNER"), juce::dontSendNotification);
  titleLabel_.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
  titleLabel_.setColour(juce::Label::textColourId, theme_.accent);
  addAndMakeVisible(titleLabel_);

  // Preset
  presetLabel_.setText(TRANS("Profile:"), juce::dontSendNotification);
  presetLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(presetLabel_);

  presetCombo_.addItem(TRANS("Peak Hour"), 1);
  presetCombo_.addItem(TRANS("Progressive Climb"), 2);
  presetCombo_.addItem(TRANS("Wave Pattern"), 3);
  presetCombo_.addItem(TRANS("Warmup"), 4);
  presetCombo_.addItem(TRANS("High-Energy Banger"), 5);
  presetCombo_.setSelectedId(1, juce::dontSendNotification);
  addAndMakeVisible(presetCombo_);

  // Duration
  durationLabel_.setText(TRANS("Duration:"), juce::dontSendNotification);
  durationLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(durationLabel_);

  durationCombo_.addItem(TRANS("30 Minutes"), 1);
  durationCombo_.addItem(TRANS("45 Minutes"), 2);
  durationCombo_.addItem(TRANS("60 Minutes"), 3);
  durationCombo_.addItem(TRANS("90 Minutes"), 4);
  durationCombo_.setSelectedId(3, juce::dontSendNotification);
  addAndMakeVisible(durationCombo_);

  // Genre
  genreLabel_.setText(TRANS("Genre:"), juce::dontSendNotification);
  genreLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(genreLabel_);

  genreEditor_.setText("Drum & Bass");
  genreEditor_.setColour(juce::TextEditor::backgroundColourId, theme_.panel);
  genreEditor_.setColour(juce::TextEditor::textColourId, theme_.text);
  addAndMakeVisible(genreEditor_);

  // Build Button
  buildButton_.onClick = [this] { planSet(); };
  buildButton_.setColour(juce::TextButton::buttonColourId, theme_.accent);
  buildButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
  addAndMakeVisible(buildButton_);

  // Summary Label
  summaryLabel_.setText(TRANS("Ready to plan set."), juce::dontSendNotification);
  summaryLabel_.setFont(juce::FontOptions(12.0f));
  summaryLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(summaryLabel_);

  // Energy Chart
  chart_.setTheme(theme_);
  addAndMakeVisible(chart_);

  // Table
  auto& header = trackTable_.getHeader();
  header.addColumn("#", ColIndex, 35, 25, 45);
  header.addColumn(TRANS("Start Time"), ColTime, 80, 60, 110);
  header.addColumn(TRANS("Artist"), ColArtist, 130, 80, 220);
  header.addColumn(TRANS("Title"), ColTitle, 170, 100, 300);
  header.addColumn("BPM", ColBpm, 55, 45, 75);
  header.addColumn("Key", ColKey, 50, 40, 70);
  header.addColumn(TRANS("Energy"), ColEnergy, 55, 45, 75);
  header.addColumn(TRANS("Transition Quality"), ColTransition, 160, 100, 300);

  trackTable_.setModel(this);
  trackTable_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  trackTable_.setOutlineThickness(0);
  addAndMakeVisible(trackTable_);
}

void SetBuilderComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  chart_.setTheme(theme);
  titleLabel_.setColour(juce::Label::textColourId, theme_.accent);
  trackTable_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  repaint();
}

void SetBuilderComponent::setBuilder(core::ISetBuilder* builder) {
  builder_ = builder;
}

void SetBuilderComponent::setCatalog(std::vector<core::TrackItem> catalog) {
  catalog_ = std::move(catalog);
}

void SetBuilderComponent::planSet() {
  if (!builder_ || catalog_.empty()) {
    summaryLabel_.setText(TRANS("No builder service or empty catalog."), juce::dontSendNotification);
    return;
  }

  core::SetBuilderRequest req;
  switch (durationCombo_.getSelectedId()) {
    case 1: req.targetDurationMinutes = 30.0; break;
    case 2: req.targetDurationMinutes = 45.0; break;
    case 3: req.targetDurationMinutes = 60.0; break;
    case 4: req.targetDurationMinutes = 90.0; break;
  }

  switch (presetCombo_.getSelectedId()) {
    case 1: req.preset = core::EnergyCurvePreset::PeakHour; break;
    case 2: req.preset = core::EnergyCurvePreset::ProgressiveClimb; break;
    case 3: req.preset = core::EnergyCurvePreset::WavePattern; break;
    case 4: req.preset = core::EnergyCurvePreset::Warmup; break;
    case 5: req.preset = core::EnergyCurvePreset::HighEnergyBanger; break;
  }

  req.targetGenre = genreEditor_.getText().toStdString();
  req.minBpm = 160.0;
  req.maxBpm = 185.0;

  plan_ = builder_->buildSet(req, catalog_);
  chart_.setPlan(plan_);
  summaryLabel_.setText(i18n::translateMessage(plan_.summary), juce::dontSendNotification);
  trackTable_.updateContent();
  trackTable_.repaint();
}

void SetBuilderComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);
  g.setColour(theme_.panel);
  g.fillRect(getLocalBounds().removeFromTop(75));
}

void SetBuilderComponent::resized() {
  auto area = getLocalBounds().reduced(8);

  // Title row
  auto titleRow = area.removeFromTop(24);
  titleLabel_.setBounds(titleRow.removeFromLeft(360));
  buildButton_.setBounds(titleRow.removeFromRight(180));

  // Controls row
  auto ctrlRow = area.removeFromTop(26);
  presetLabel_.setBounds(ctrlRow.removeFromLeft(50));
  presetCombo_.setBounds(ctrlRow.removeFromLeft(140));
  ctrlRow.removeFromLeft(12);

  durationLabel_.setBounds(ctrlRow.removeFromLeft(60));
  durationCombo_.setBounds(ctrlRow.removeFromLeft(110));
  ctrlRow.removeFromLeft(12);

  genreLabel_.setBounds(ctrlRow.removeFromLeft(50));
  genreEditor_.setBounds(ctrlRow.removeFromLeft(130));

  // Summary row
  area.removeFromTop(4);
  summaryLabel_.setBounds(area.removeFromTop(18));

  // Chart (top half)
  area.removeFromTop(6);
  const int chartHeight = std::min(160, area.getHeight() / 2);
  chart_.setBounds(area.removeFromTop(chartHeight));

  // Track list table (bottom half)
  area.removeFromTop(8);
  trackTable_.setBounds(area);
}

int SetBuilderComponent::getNumRows() {
  return static_cast<int>(plan_.tracks.size());
}

void SetBuilderComponent::paintRowBackground(
    juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected) {
  if (rowIsSelected) {
    g.fillAll(theme_.panel.brighter(0.15f));
  } else if (rowNumber % 2 == 1) {
    g.fillAll(theme_.background.brighter(0.02f));
  }
}

void SetBuilderComponent::paintCell(
    juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/) {
  if (rowNumber < 0 || rowNumber >= static_cast<int>(plan_.tracks.size())) {
    return;
  }

  const auto& entry = plan_.tracks[static_cast<std::size_t>(rowNumber)];
  g.setFont(juce::FontOptions(12.0f));

  auto cellArea = juce::Rectangle<int>(4, 0, width - 8, height);

  switch (columnId) {
    case ColIndex:
      g.setColour(theme_.textDim);
      g.drawText(juce::String(entry.trackIndex), cellArea, juce::Justification::centredLeft);
      break;

    case ColTime: {
      const int totalSec = static_cast<int>(entry.startTimeMinutes * 60.0);
      const int min = totalSec / 60;
      const int sec = totalSec % 60;
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d:%02d", min, sec);
      g.setColour(theme_.accent);
      g.drawText(buf, cellArea, juce::Justification::centredLeft);
      break;
    }

    case ColArtist:
      g.setColour(theme_.text);
      g.drawText(entry.track.artist, cellArea, juce::Justification::centredLeft, true);
      break;

    case ColTitle:
      g.setColour(theme_.text);
      g.drawText(entry.track.title, cellArea, juce::Justification::centredLeft, true);
      break;

    case ColBpm:
      g.setColour(theme_.text);
      g.drawText(juce::String(entry.track.bpm, 1), cellArea, juce::Justification::centredLeft);
      break;

    case ColKey:
      g.setColour(entry.transitionScore.isHarmonic ? theme_.accent : theme_.text);
      g.drawText(entry.track.key, cellArea, juce::Justification::centredLeft);
      break;

    case ColEnergy:
      g.setColour(theme_.meterYellow);
      g.drawText(juce::String(entry.actualEnergy, 1), cellArea, juce::Justification::centredLeft);
      break;

    case ColTransition: {
      const int pct = static_cast<int>(std::round(entry.transitionScore.overallScore * 100.0f));
      const auto scoreCol = (pct >= 85) ? theme_.accent : ((pct >= 70) ? theme_.meterYellow : theme_.textDim);
      g.setColour(scoreCol);
      g.drawText(juce::String(pct) + "% (" + entry.transitionScore.keyRelation + ")",
                 cellArea, juce::Justification::centredLeft, true);
      break;
    }
  }
}

}  // namespace zyron::ui
