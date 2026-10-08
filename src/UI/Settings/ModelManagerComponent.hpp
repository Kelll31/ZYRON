// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Core/AI/ModelTypes.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// AI Model Manager settings drawer and inspector (SPEC sections 73, 74, ADR-0013).
class ModelManagerComponent : public juce::Component,
                             public juce::TableListBoxModel {
 public:
  enum ColumnId {
    ColName = 1,
    ColTask,
    ColVersion,
    ColLicense,
    ColSize,
    ColStatus,
    ColBackends
  };

  explicit ModelManagerComponent(std::shared_ptr<core::IModelManager> modelManager,
                                 Theme theme = Theme::dark());
  ~ModelManagerComponent() override;

  void setTheme(const Theme& theme);
  void refresh();

  // Callbacks
  std::function<void(const std::string& modelId)> onImportRequested;
  std::function<void(const std::string& modelId)> onRemoveRequested;

  // juce::Component overrides
  void paint(juce::Graphics& g) override;
  void resized() override;

  // juce::TableListBoxModel overrides
  int getNumRows() override;
  void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
  void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
  void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;

  [[nodiscard]] const std::vector<core::ModelMetadata>& models() const noexcept { return cachedModels_; }

 private:
  std::shared_ptr<core::IModelManager> modelManager_;
  Theme theme_;

  std::vector<core::ModelMetadata> cachedModels_;

  juce::Label titleLabel_;
  juce::Label headerInfoLabel_;
  juce::TextButton refreshButton_{TRANS("Refresh")};
  juce::TextButton importButton_{TRANS("Import Local...")};
  juce::TextButton removeButton_{TRANS("Remove")};
  juce::TableListBox table_;
};

}  // namespace zyron::ui
