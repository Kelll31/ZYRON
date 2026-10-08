// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Settings/BackgroundQueueComponent.hpp"

#include <cmath>

namespace zyron::ui {

BackgroundQueueComponent::BackgroundQueueComponent(std::shared_ptr<core::IBackgroundQueueManager> queueManager,
                                                   Theme theme)
    : queueManager_(std::move(queueManager)), theme_(theme) {
  titleLabel_.setText("AI Background Task Queue (SPEC \xc2\xa7""41)", juce::dontSendNotification);
  titleLabel_.setFont(juce::FontOptions{18.0F, juce::Font::bold});
  titleLabel_.setColour(juce::Label::textColourId, theme_.text);
  addAndMakeVisible(titleLabel_);

  activeCountLabel_.setText("Active: 0", juce::dontSendNotification);
  activeCountLabel_.setFont(juce::FontOptions{12.0F});
  activeCountLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(activeCountLabel_);

  clearFinishedButton_.onClick = [this] {
    if (queueManager_ != nullptr) {
      queueManager_->clearFinishedJobs();
    }
    refresh();
  };
  addAndMakeVisible(clearFinishedButton_);

  cancelSelectedButton_.onClick = [this] {
    const int row = table_.getSelectedRow();
    if (row >= 0 && row < static_cast<int>(cachedJobs_.size())) {
      const auto id = cachedJobs_[static_cast<std::size_t>(row)].id;
      if (queueManager_ != nullptr) {
        queueManager_->cancelJob(id);
      }
      refresh();
    }
  };
  addAndMakeVisible(cancelSelectedButton_);

  auto& header = table_.getHeader();
  header.addColumn("ID", ColId, 50, 40, 80);
  header.addColumn("Task Name", ColName, 220, 150, 350);
  header.addColumn("Priority", ColPriority, 90, 70, 120);
  header.addColumn("Device", ColDevice, 80, 60, 110);
  header.addColumn("Status", ColStatus, 95, 75, 130);
  header.addColumn("Progress", ColProgress, 180, 120, 300);

  table_.setModel(this);
  table_.setMultipleSelectionEnabled(false);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  addAndMakeVisible(table_);

  refresh();
  startTimerHz(10);  // 10 Hz refresh for smooth task progress tracking
}

BackgroundQueueComponent::~BackgroundQueueComponent() {
  stopTimer();
  table_.setModel(nullptr);
}

void BackgroundQueueComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  titleLabel_.setColour(juce::Label::textColourId, theme_.text);
  activeCountLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  repaint();
}

void BackgroundQueueComponent::refresh() {
  if (queueManager_ != nullptr) {
    cachedJobs_ = queueManager_->allJobs();
    const auto activeCount = queueManager_->activeJobCount();
    activeCountLabel_.setText("Active tasks: " + std::to_string(activeCount), juce::dontSendNotification);
  }
  table_.updateContent();
  table_.repaint();
}

void BackgroundQueueComponent::timerCallback() {
  refresh();
}

void BackgroundQueueComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);
}

void BackgroundQueueComponent::resized() {
  auto bounds = getLocalBounds().reduced(8);

  auto topRow = bounds.removeFromTop(28);
  titleLabel_.setBounds(topRow.removeFromLeft(300));

  cancelSelectedButton_.setBounds(topRow.removeFromRight(100));
  topRow.removeFromRight(6);
  clearFinishedButton_.setBounds(topRow.removeFromRight(110));

  bounds.removeFromTop(4);
  activeCountLabel_.setBounds(bounds.removeFromTop(20));
  bounds.removeFromTop(8);

  table_.setBounds(bounds);
}

int BackgroundQueueComponent::getNumRows() {
  return static_cast<int>(cachedJobs_.size());
}

void BackgroundQueueComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/,
                                                 bool rowIsSelected) {
  if (rowIsSelected) {
    g.fillAll(theme_.accent.withAlpha(0.25F));
  } else if ((rowNumber % 2) != 0) {
    g.fillAll(theme_.panel.withAlpha(0.5F));
  } else {
    g.fillAll(theme_.background);
  }
}

void BackgroundQueueComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height,
                                        bool /*rowIsSelected*/) {
  if (rowNumber < 0 || rowNumber >= static_cast<int>(cachedJobs_.size())) {
    return;
  }

  const auto& item = cachedJobs_[static_cast<std::size_t>(rowNumber)];
  auto cellBounds = juce::Rectangle<int>(4, 0, width - 8, height);
  g.setFont(juce::FontOptions{12.0F});

  switch (columnId) {
    case ColId:
      g.setColour(theme_.textDim);
      g.drawText(std::to_string(item.id), cellBounds, juce::Justification::centredLeft, true);
      break;

    case ColName:
      g.setColour(theme_.text);
      g.drawText(item.name, cellBounds, juce::Justification::centredLeft, true);
      break;

    case ColPriority:
      if (item.priority == core::QueuePriority::Interactive) {
        g.setColour(theme_.cueActive);  // Orange/amber for interactive priority
      } else {
        g.setColour(theme_.textDim);
      }
      g.drawText(std::string(core::queuePriorityName(item.priority)), cellBounds,
                 juce::Justification::centredLeft, true);
      break;

    case ColDevice:
      g.setColour(theme_.deckA);  // Cyan for GPU devices
      g.drawText(item.deviceName, cellBounds, juce::Justification::centredLeft, true);
      break;

    case ColStatus: {
      juce::Colour statusCol = theme_.textDim;
      if (item.status == core::QueueItemStatus::Running) {
        statusCol = theme_.playActive;
      } else if (item.status == core::QueueItemStatus::Completed) {
        statusCol = theme_.meterGreen;
      } else if (item.status == core::QueueItemStatus::Failed) {
        statusCol = theme_.meterRed;
      } else if (item.status == core::QueueItemStatus::Cancelled) {
        statusCol = theme_.textDim;
      }
      g.setColour(statusCol);
      g.drawText(std::string(core::queueItemStatusName(item.status)), cellBounds,
                 juce::Justification::centredLeft, true);
      break;
    }

    case ColProgress: {
      const auto barRect = cellBounds.reduced(2, 4);
      g.setColour(theme_.panel);
      g.fillRoundedRectangle(barRect.toFloat(), 3.0F);

      const float pct = std::clamp(item.progress, 0.0F, 1.0F);
      if (pct > 0.0F) {
        auto fillRect = barRect.toFloat();
        fillRect.setWidth(fillRect.getWidth() * pct);
        g.setColour(theme_.accent);
        g.fillRoundedRectangle(fillRect, 3.0F);
      }

      const int pctInt = static_cast<int>(std::round(pct * 100.0F));
      g.setColour(theme_.text);
      g.drawText(std::to_string(pctInt) + "%", barRect, juce::Justification::centred, false);
      break;
    }

    default:
      break;
  }
}

}  // namespace zyron::ui
