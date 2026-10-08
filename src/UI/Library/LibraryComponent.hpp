// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/Ids.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Track library browser, table view, and search interface (SPEC sections 28, 30).
class LibraryComponent : public juce::Component,
                         public juce::TableListBoxModel,
                         public juce::TextEditor::Listener {
 public:
  enum ColumnId {
    ColIndex = 1,
    ColTitle,
    ColArtist,
    ColBpm,
    ColKey,
    ColEnergy,
    ColDuration,
    ColGenre,
    ColPath
  };

  explicit LibraryComponent(std::shared_ptr<core::ILibrarySource> source,
                            Theme theme = Theme::dark());
  ~LibraryComponent() override;

  void setTheme(const Theme& theme);
  void refreshTracks();

  // Callbacks
  std::function<void(const core::TrackItem& track, core::DeckId deck)> onTrackLoadRequested;
  std::function<void(const std::string& folderPath)> onScanRequested;

  // TableListBoxModel overrides
  int getNumRows() override;
  void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
  void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
  void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;
  void sortOrderChanged(int newSortColumnId, bool isForwards) override;

  // TextEditor::Listener overrides
  void textEditorTextChanged(juce::TextEditor& editor) override;
  void textEditorReturnKeyPressed(juce::TextEditor& editor) override;

  void resized() override;
  void paint(juce::Graphics& g) override;

 private:
  void filterTracks(const juce::String& query);
  void sortCurrentTracks();
  void loadSelectedTrackToDeck(core::DeckId deck);

  std::shared_ptr<core::ILibrarySource> source_;
  Theme theme_;

  juce::TextEditor searchBox_;
  juce::TextButton scanButton_{"Scan Folder..."};
  juce::TextButton loadDeckAButton_{"Load A"};
  juce::TextButton loadDeckBButton_{"Load B"};
  juce::Label statusLabel_;

  juce::TableListBox table_;
  std::vector<core::TrackItem> allTracks_;
  std::vector<core::TrackItem> displayedTracks_;

  int sortColumnId_{ColTitle};
  bool sortAscending_{true};

  std::unique_ptr<juce::FileChooser> fileChooser_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryComponent)
};

}  // namespace zyron::ui
