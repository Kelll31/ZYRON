// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

#include "Core/AI/BackgroundQueueTypes.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Real-time AI background queue and multi-GPU task inspector (SPEC sections 36, 41, ROADMAP P5-09).
class BackgroundQueueComponent : public juce::Component,
                                public juce::TableListBoxModel,
                                private juce::Timer {
 public:
  enum ColumnId {
    ColId = 1,
    ColName,
    ColPriority,
    ColDevice,
    ColStatus,
    ColProgress
  };

  explicit BackgroundQueueComponent(std::shared_ptr<core::IBackgroundQueueManager> queueManager,
                                   Theme theme = Theme::dark());
  ~BackgroundQueueComponent() override;

  void setTheme(const Theme& theme);
  void refresh();

  // juce::Component overrides
  void paint(juce::Graphics& g) override;
  void resized() override;

  // juce::TableListBoxModel overrides
  int getNumRows() override;
  void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
  void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;

  [[nodiscard]] const std::vector<core::QueueJobSnapshot>& jobs() const noexcept { return cachedJobs_; }

 private:
  void timerCallback() override;

  std::shared_ptr<core::IBackgroundQueueManager> queueManager_;
  Theme theme_;

  std::vector<core::QueueJobSnapshot> cachedJobs_;

  juce::Label titleLabel_;
  juce::Label activeCountLabel_;
  juce::TextButton clearFinishedButton_{TRANS("Clear Finished")};
  juce::TextButton cancelSelectedButton_{TRANS("Cancel Task")};
  juce::TableListBox table_;
};

}  // namespace zyron::ui
