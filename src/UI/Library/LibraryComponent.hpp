// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <deque>
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
    ColPath,
    ColStatus
  };

  explicit LibraryComponent(std::shared_ptr<core::ILibrarySource> source,
                            Theme theme = Theme::dark());
  ~LibraryComponent() override;

  void setTheme(const Theme& theme);
  void refreshTracks();
  /// Reads the scanner's progress: shows it in the status line and reloads the table when tracks were added.
  void updateScanStatus();
  /// Shows the Load C / Load D buttons in 4-deck layout.
  void setFourDecks(bool fourDecks);

  // Callbacks
  std::function<void(const core::TrackItem& track, core::DeckId deck)> onTrackLoadRequested;
  std::function<void(const std::string& folderPath)> onScanRequested;
  /// "Play this next": mix the track in live (Automix on: as the next track at once).
  std::function<void(const core::TrackItem& track)> onMixNextRequested;

  // TableListBoxModel overrides
  int getNumRows() override;
  void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
  void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
  void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;
  void sortOrderChanged(int newSortColumnId, bool isForwards) override;
  void cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;

  // TextEditor::Listener overrides
  void textEditorTextChanged(juce::TextEditor& editor) override;
  void textEditorReturnKeyPressed(juce::TextEditor& editor) override;

  void resized() override;
  void paint(juce::Graphics& g) override;

 private:
  void filterTracks(const juce::String& query);
  void sortCurrentTracks();
  void loadSelectedTrackToDeck(core::DeckId deck);
  void updateStatusText();
  void showTrackMenu(int row);
  void loadTrackToDeck(const core::TrackItem& track, core::DeckId deck);

  std::shared_ptr<core::ILibrarySource> source_;
  Theme theme_;

  juce::TextEditor searchBox_;
  juce::TextButton scanButton_{TRANS("Scan Folder...")};
  juce::TextButton loadDeckAButton_{TRANS("Load A")};
  juce::TextButton loadDeckBButton_{TRANS("Load B")};
  juce::TextButton mixNextButton_{TRANS("Mix next")};
  juce::TextButton loadDeckCButton_{TRANS("Load C")};
  juce::TextButton loadDeckDButton_{TRANS("Load D")};
  juce::Label statusLabel_;

  juce::TableListBox table_;
  std::vector<core::TrackItem> allTracks_;
  std::vector<core::TrackItem> displayedTracks_;

  int sortColumnId_{ColTitle};
  bool sortAscending_{true};

  std::unique_ptr<juce::FileChooser> fileChooser_;

  struct PendingSample {
    std::chrono::steady_clock::time_point at;
    std::size_t pending;
  };
  std::deque<PendingSample> pendingHistory_;  // for the time-left estimate of the background analysis
  core::LibraryScanStatus scan_;
  bool fourDecks_{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryComponent)
};

}  // namespace zyron::ui
