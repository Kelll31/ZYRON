// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Settings/ModelManagerComponent.hpp"

#include <iomanip>
#include <sstream>

namespace zyron::ui {

namespace {

std::string formatSize(std::uint64_t bytes) {
  if (bytes == 0) {
    return "0 B";
  }
  constexpr double kMB = 1024.0 * 1024.0;
  constexpr double kGB = 1024.0 * 1024.0 * 1024.0;
  const auto dBytes = static_cast<double>(bytes);

  std::ostringstream ss;
  ss << std::fixed << std::setprecision(1);
  if (dBytes >= kGB) {
    ss << (dBytes / kGB) << " GB";
  } else {
    ss << (dBytes / kMB) << " MB";
  }
  return ss.str();
}

}  // namespace

ModelManagerComponent::ModelManagerComponent(std::shared_ptr<core::IModelManager> modelManager,
                                             Theme theme)
    : modelManager_(std::move(modelManager)), theme_(theme) {
  titleLabel_.setText("AI Model Manager (SPEC \xc2\xa7""74)", juce::dontSendNotification);
  titleLabel_.setFont(juce::FontOptions{18.0F, juce::Font::bold});
  titleLabel_.setColour(juce::Label::textColourId, theme_.text);
  addAndMakeVisible(titleLabel_);

  headerInfoLabel_.setText("Manage models for Stems, BPM, Key and Analysis. Permissive weights verified.",
                           juce::dontSendNotification);
  headerInfoLabel_.setFont(juce::FontOptions{12.0F});
  headerInfoLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(headerInfoLabel_);

  refreshButton_.onClick = [this] { refresh(); };
  addAndMakeVisible(refreshButton_);

  importButton_.onClick = [this] {
    const int row = table_.getSelectedRow();
    if (row >= 0 && row < static_cast<int>(cachedModels_.size()) && onImportRequested) {
      onImportRequested(cachedModels_[static_cast<std::size_t>(row)].id);
    }
  };
  addAndMakeVisible(importButton_);

  removeButton_.onClick = [this] {
    const int row = table_.getSelectedRow();
    if (row >= 0 && row < static_cast<int>(cachedModels_.size())) {
      const auto& id = cachedModels_[static_cast<std::size_t>(row)].id;
      if (modelManager_ != nullptr) {
        modelManager_->removeModel(id);
      }
      if (onRemoveRequested) {
        onRemoveRequested(id);
      }
      refresh();
    }
  };
  addAndMakeVisible(removeButton_);

  auto& header = table_.getHeader();
  header.addColumn("Model", ColName, 200, 120, 300);
  header.addColumn("Task", ColTask, 150, 100, 200);
  header.addColumn("Ver", ColVersion, 60, 50, 80);
  header.addColumn("License", ColLicense, 120, 80, 160);
  header.addColumn("Size", ColSize, 85, 60, 120);
  header.addColumn("Status", ColStatus, 110, 80, 150);
  header.addColumn("Compatibility", ColBackends, 180, 100, 300);

  table_.setModel(this);
  table_.setMultipleSelectionEnabled(false);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  addAndMakeVisible(table_);

  refresh();
}

ModelManagerComponent::~ModelManagerComponent() {
  table_.setModel(nullptr);
}

void ModelManagerComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  titleLabel_.setColour(juce::Label::textColourId, theme_.text);
  headerInfoLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  repaint();
}

void ModelManagerComponent::refresh() {
  if (modelManager_ != nullptr) {
    modelManager_->refresh();
    cachedModels_ = modelManager_->availableModels();
  }
  table_.updateContent();
  table_.repaint();
}

void ModelManagerComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);
}

void ModelManagerComponent::resized() {
  auto bounds = getLocalBounds().reduced(8);

  auto topRow = bounds.removeFromTop(28);
  titleLabel_.setBounds(topRow.removeFromLeft(300));

  refreshButton_.setBounds(topRow.removeFromRight(80));
  topRow.removeFromRight(6);
  removeButton_.setBounds(topRow.removeFromRight(80));
  topRow.removeFromRight(6);
  importButton_.setBounds(topRow.removeFromRight(120));

  bounds.removeFromTop(4);
  headerInfoLabel_.setBounds(bounds.removeFromTop(20));
  bounds.removeFromTop(8);

  table_.setBounds(bounds);
}

int ModelManagerComponent::getNumRows() {
  return static_cast<int>(cachedModels_.size());
}

void ModelManagerComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/,
                                               bool rowIsSelected) {
  if (rowIsSelected) {
    g.fillAll(theme_.accent.withAlpha(0.25F));
  } else if ((rowNumber % 2) != 0) {
    g.fillAll(theme_.panel.withAlpha(0.5F));
  } else {
    g.fillAll(theme_.background);
  }
}

void ModelManagerComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height,
                                      bool /*rowIsSelected*/) {
  if (rowNumber < 0 || rowNumber >= static_cast<int>(cachedModels_.size())) {
    return;
  }

  const auto& item = cachedModels_[static_cast<std::size_t>(rowNumber)];
  auto cellBounds = juce::Rectangle<int>(4, 0, width - 8, height);
  g.setFont(juce::FontOptions{12.0F});

  switch (columnId) {
    case ColName:
      g.setColour(theme_.text);
      g.drawText(item.name, cellBounds, juce::Justification::centredLeft, true);
      break;

    case ColTask:
      g.setColour(theme_.textDim);
      g.drawText(std::string(core::modelTaskName(item.task)), cellBounds, juce::Justification::centredLeft, true);
      break;

    case ColVersion:
      g.setColour(theme_.textDim);
      g.drawText(item.version, cellBounds, juce::Justification::centredLeft, true);
      break;

    case ColLicense:
      if (!item.isPermissive) {
        g.setColour(juce::Colour{0xffff9f0a});  // Orange highlight for non-commercial notice
      } else {
        g.setColour(theme_.textDim);
      }
      g.drawText(item.license, cellBounds, juce::Justification::centredLeft, true);
      break;

    case ColSize:
      g.setColour(theme_.textDim);
      g.drawText(formatSize(item.sizeBytes), cellBounds, juce::Justification::centredLeft, true);
      break;

    case ColStatus: {
      juce::Colour statusCol = theme_.textDim;
      if (item.status == core::ModelInstallStatus::Ready) {
        statusCol = theme_.meterGreen;
      } else if (item.status == core::ModelInstallStatus::Installed) {
        statusCol = theme_.deckA;
      } else if (item.status == core::ModelInstallStatus::Corrupted) {
        statusCol = theme_.meterRed;
      }
      g.setColour(statusCol);
      g.drawText(std::string(core::modelInstallStatusName(item.status)), cellBounds,
                 juce::Justification::centredLeft, true);
      break;
    }

    case ColBackends:
      g.setColour(theme_.textDim);
      g.drawText(item.supportedBackends, cellBounds, juce::Justification::centredLeft, true);
      break;

    default:
      break;
  }
}

void ModelManagerComponent::cellDoubleClicked(int rowNumber, int /*columnId*/, const juce::MouseEvent& /*e*/) {
  if (rowNumber >= 0 && rowNumber < static_cast<int>(cachedModels_.size()) && onImportRequested) {
    onImportRequested(cachedModels_[static_cast<std::size_t>(rowNumber)].id);
  }
}

}  // namespace zyron::ui
