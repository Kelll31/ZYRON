// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Library/LibraryComponent.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace zyron::ui {

LibraryComponent::LibraryComponent(std::shared_ptr<core::ILibrarySource> source, Theme theme)
    : source_(std::move(source)), theme_(theme) {
  // 1. Search Box
  searchBox_.setTextToShowWhenEmpty("Search tracks by artist, title, genre, BPM, key, path...",
                                    theme_.textDim);
  searchBox_.addListener(this);
  searchBox_.setColour(juce::TextEditor::backgroundColourId, theme_.panel);
  searchBox_.setColour(juce::TextEditor::textColourId, theme_.text);
  searchBox_.setColour(juce::TextEditor::outlineColourId, theme_.panel.brighter(0.1f));
  addAndMakeVisible(searchBox_);

  // 2. Buttons
  scanButton_.onClick = [this] {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Select Music Folder to Scan", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    const auto folderFlags = juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectDirectories;
    fileChooser_->launchAsync(folderFlags, [this](const juce::FileChooser& chooser) {
      const auto result = chooser.getResult();
      if (result.exists() && result.isDirectory()) {
        const std::string path = result.getFullPathName().toStdString();
        if (source_) {
          source_->requestScan(path);
        }
        if (onScanRequested) {
          onScanRequested(path);
        }
        refreshTracks();
      }
    });
  };
  addAndMakeVisible(scanButton_);

  loadDeckAButton_.onClick = [this] { loadSelectedTrackToDeck(core::DeckId::A); };
  loadDeckAButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  loadDeckAButton_.setColour(juce::TextButton::textColourOffId, theme_.accent);
  addAndMakeVisible(loadDeckAButton_);

  loadDeckBButton_.onClick = [this] { loadSelectedTrackToDeck(core::DeckId::B); };
  loadDeckBButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  loadDeckBButton_.setColour(juce::TextButton::textColourOffId, theme_.accent);
  addAndMakeVisible(loadDeckBButton_);

  // 3. Status Label
  statusLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  statusLabel_.setFont(juce::FontOptions(12.0f));
  addAndMakeVisible(statusLabel_);

  // 4. Table Header Columns
  constexpr int colFlags = juce::TableHeaderComponent::visible |
                           juce::TableHeaderComponent::resizable |
                           juce::TableHeaderComponent::sortable;

  auto& header = table_.getHeader();
  header.addColumn("#", ColIndex, 36, 25, 50, juce::TableHeaderComponent::notSortable);
  header.addColumn("Title", ColTitle, 200, 100, 400, colFlags);
  header.addColumn("Artist", ColArtist, 150, 80, 300, colFlags);
  header.addColumn("BPM", ColBpm, 65, 50, 90, colFlags);
  header.addColumn("Key", ColKey, 65, 50, 90, colFlags);
  header.addColumn("Energy", ColEnergy, 65, 50, 90, colFlags);
  header.addColumn("Time", ColDuration, 60, 45, 80, colFlags);
  header.addColumn("Genre", ColGenre, 110, 60, 200, colFlags);
  header.addColumn("File Path", ColPath, 250, 100, 600, colFlags);

  table_.setModel(this);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  table_.setOutlineThickness(0);
  addAndMakeVisible(table_);

  refreshTracks();
}

LibraryComponent::~LibraryComponent() {
  searchBox_.removeListener(this);
  table_.setModel(nullptr);
}

void LibraryComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  searchBox_.setColour(juce::TextEditor::backgroundColourId, theme_.panel);
  searchBox_.setColour(juce::TextEditor::textColourId, theme_.text);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  repaint();
}

void LibraryComponent::refreshTracks() {
  if (source_) {
    allTracks_ = source_->listAll();
  }
  filterTracks(searchBox_.getText());
}

void LibraryComponent::filterTracks(const juce::String& query) {
  const auto trimmed = query.trim();
  if (trimmed.isEmpty()) {
    displayedTracks_ = allTracks_;
  } else {
    displayedTracks_.clear();
    const auto lowerQuery = trimmed.toLowerCase().toStdString();
    for (const auto& t : allTracks_) {
      juce::String combined;
      combined << t.title << " " << t.artist << " " << t.album << " "
               << t.genre << " " << t.key << " " << t.filepath;
      if (combined.toLowerCase().contains(lowerQuery)) {
        displayedTracks_.push_back(t);
      }
    }
  }

  sortCurrentTracks();
  table_.updateContent();

  juce::String status;
  status << displayedTracks_.size() << " tracks";
  if (displayedTracks_.size() != allTracks_.size()) {
    status << " (filtered from " << allTracks_.size() << ")";
  }
  statusLabel_.setText(status, juce::dontSendNotification);
}

void LibraryComponent::sortCurrentTracks() {
  std::sort(displayedTracks_.begin(), displayedTracks_.end(),
            [this](const core::TrackItem& a, const core::TrackItem& b) {
              int cmp = 0;
              switch (sortColumnId_) {
                case ColTitle: cmp = a.title.compare(b.title); break;
                case ColArtist: cmp = a.artist.compare(b.artist); break;
                case ColBpm: cmp = (a.bpm < b.bpm) ? -1 : ((a.bpm > b.bpm) ? 1 : 0); break;
                case ColKey: cmp = a.key.compare(b.key); break;
                case ColEnergy: cmp = (a.energy < b.energy) ? -1 : ((a.energy > b.energy) ? 1 : 0); break;
                case ColDuration: cmp = (a.durationSec < b.durationSec) ? -1 : ((a.durationSec > b.durationSec) ? 1 : 0); break;
                case ColGenre: cmp = a.genre.compare(b.genre); break;
                case ColPath: cmp = a.filepath.compare(b.filepath); break;
                default: cmp = a.title.compare(b.title); break;
              }
              return sortAscending_ ? (cmp < 0) : (cmp > 0);
            });
}

int LibraryComponent::getNumRows() {
  return static_cast<int>(displayedTracks_.size());
}

void LibraryComponent::paintRowBackground(juce::Graphics& g, int rowNumber,
                                          int width, int height, bool rowIsSelected) {
  juce::ignoreUnused(width, height);
  if (rowIsSelected) {
    g.fillAll(theme_.accent.withAlpha(0.25f));
  } else if (rowNumber % 2 == 1) {
    g.fillAll(theme_.panel.withAlpha(0.35f));
  }
}

void LibraryComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId,
                                 int width, int height, bool rowIsSelected) {
  if (rowNumber < 0 || rowNumber >= static_cast<int>(displayedTracks_.size())) {
    return;
  }

  const auto& track = displayedTracks_[static_cast<std::size_t>(rowNumber)];
  g.setColour(rowIsSelected ? theme_.accent : theme_.text);
  g.setFont(12.0f);

  const auto cellBounds = juce::Rectangle<int>(4, 0, width - 8, height);

  switch (columnId) {
    case ColIndex:
      g.setColour(theme_.textDim);
      g.drawText(juce::String(rowNumber + 1), cellBounds, juce::Justification::centredRight, true);
      break;
    case ColTitle:
      g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
      g.drawText(track.title.empty() ? "(Untitled)" : track.title, cellBounds, juce::Justification::centredLeft, true);
      break;
    case ColArtist:
      g.drawText(track.artist, cellBounds, juce::Justification::centredLeft, true);
      break;
    case ColBpm:
      if (track.bpm > 0.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << track.bpm;
        g.drawText(ss.str(), cellBounds, juce::Justification::centredRight, true);
      } else {
        g.setColour(theme_.textDim);
        g.drawText("--", cellBounds, juce::Justification::centredRight, true);
      }
      break;
    case ColKey:
      if (!track.key.empty()) {
        g.setColour(theme_.accent);
        g.drawText(track.key, cellBounds, juce::Justification::centred, true);
      } else {
        g.setColour(theme_.textDim);
        g.drawText("--", cellBounds, juce::Justification::centred, true);
      }
      break;
    case ColEnergy:
      if (track.energy > 0.0) {
        // Colored badge based on energy 1.0..10.0
        juce::Colour eCol = theme_.waveformLow;
        if (track.energy < 4.0) eCol = theme_.waveformHigh;
        else if (track.energy < 7.0) eCol = theme_.waveformMid;

        g.setColour(eCol);
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << track.energy;
        g.drawText(ss.str(), cellBounds, juce::Justification::centred, true);
      } else {
        g.setColour(theme_.textDim);
        g.drawText("--", cellBounds, juce::Justification::centred, true);
      }
      break;
    case ColDuration:
      g.drawText(track.formatDuration(), cellBounds, juce::Justification::centredRight, true);
      break;
    case ColGenre:
      g.setColour(theme_.textDim);
      g.drawText(track.genre, cellBounds, juce::Justification::centredLeft, true);
      break;
    case ColPath:
      g.setColour(theme_.textDim);
      g.drawText(track.filepath, cellBounds, juce::Justification::centredLeft, true);
      break;
    default:
      break;
  }
}

void LibraryComponent::cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& e) {
  juce::ignoreUnused(columnId, e);
  if (rowNumber >= 0 && rowNumber < static_cast<int>(displayedTracks_.size())) {
    loadSelectedTrackToDeck(core::DeckId::A);
  }
}

void LibraryComponent::sortOrderChanged(int newSortColumnId, bool isForwards) {
  sortColumnId_ = newSortColumnId;
  sortAscending_ = isForwards;
  sortCurrentTracks();
  table_.updateContent();
}

void LibraryComponent::textEditorTextChanged(juce::TextEditor& editor) {
  filterTracks(editor.getText());
}

void LibraryComponent::textEditorReturnKeyPressed(juce::TextEditor& editor) {
  juce::ignoreUnused(editor);
  loadSelectedTrackToDeck(core::DeckId::A);
}

void LibraryComponent::loadSelectedTrackToDeck(core::DeckId deck) {
  const int selectedRow = table_.getSelectedRow();
  if (selectedRow >= 0 && selectedRow < static_cast<int>(displayedTracks_.size())) {
    const auto& track = displayedTracks_[static_cast<std::size_t>(selectedRow)];
    if (onTrackLoadRequested) {
      onTrackLoadRequested(track, deck);
    }
  }
}

void LibraryComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);
}

void LibraryComponent::resized() {
  auto bounds = getLocalBounds().reduced(6);

  // Top control bar (height 32px)
  auto topBar = bounds.removeFromTop(32);
  scanButton_.setBounds(topBar.removeFromLeft(110));
  topBar.removeFromLeft(8);

  loadDeckAButton_.setBounds(topBar.removeFromLeft(70));
  topBar.removeFromLeft(4);
  loadDeckBButton_.setBounds(topBar.removeFromLeft(70));
  topBar.removeFromLeft(12);

  statusLabel_.setBounds(topBar.removeFromRight(140));
  searchBox_.setBounds(topBar);

  bounds.removeFromTop(6);
  table_.setBounds(bounds);
}

}  // namespace zyron::ui
