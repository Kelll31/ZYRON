// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Library/LibraryComponent.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace zyron::ui {

LibraryComponent::LibraryComponent(std::shared_ptr<core::ILibrarySource> source, Theme theme)
    : source_(std::move(source)), theme_(theme) {
  // 1. Search Box
  searchBox_.setTextToShowWhenEmpty(TRANS("Search tracks by artist, title, genre, BPM, key, path..."),
                                    theme_.textDim);
  searchBox_.addListener(this);
  searchBox_.setColour(juce::TextEditor::backgroundColourId, theme_.panel);
  searchBox_.setColour(juce::TextEditor::textColourId, theme_.text);
  searchBox_.setColour(juce::TextEditor::outlineColourId, theme_.panel.brighter(0.1f));
  addAndMakeVisible(searchBox_);

  // 2. Buttons
  scanButton_.onClick = [this] {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        TRANS("Select Music Folder to Scan"), juce::File::getSpecialLocation(juce::File::userMusicDirectory));
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

  mixNextButton_.setColour(juce::TextButton::buttonColourId, theme_.panel);
  mixNextButton_.setColour(juce::TextButton::textColourOffId, theme_.playActive);
  mixNextButton_.setTooltip(TRANS("Mix the selected track in live: the playing deck is mixed into it"));
  mixNextButton_.onClick = [this] {
    const int row = table_.getSelectedRow();
    if (row >= 0 && row < static_cast<int>(displayedTracks_.size()) && onMixNextRequested) {
      onMixNextRequested(displayedTracks_[static_cast<std::size_t>(row)]);
    }
  };
  addAndMakeVisible(mixNextButton_);

  for (auto* button : {&loadDeckCButton_, &loadDeckDButton_}) {
    button->setColour(juce::TextButton::buttonColourId, theme_.panel);
    button->setColour(juce::TextButton::textColourOffId, theme_.accent);
    addChildComponent(*button);
  }
  loadDeckCButton_.onClick = [this] { loadSelectedTrackToDeck(core::DeckId::C); };
  loadDeckDButton_.onClick = [this] { loadSelectedTrackToDeck(core::DeckId::D); };

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
  header.addColumn(TRANS("Title"), ColTitle, 200, 100, 400, colFlags);
  header.addColumn(TRANS("Artist"), ColArtist, 150, 80, 300, colFlags);
  header.addColumn("BPM", ColBpm, 65, 50, 90, colFlags);
  header.addColumn("Key", ColKey, 65, 50, 90, colFlags);
  header.addColumn(TRANS("Energy"), ColEnergy, 65, 50, 90, colFlags);
  header.addColumn(TRANS("Time"), ColDuration, 60, 45, 80, colFlags);
  header.addColumn(TRANS("Status"), ColStatus, 100, 70, 160, colFlags);
  header.addColumn(TRANS("Genre"), ColGenre, 110, 60, 200, colFlags);
  header.addColumn(TRANS("File Path"), ColPath, 250, 100, 600, colFlags);

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

  updateStatusText();
}

void LibraryComponent::updateStatusText() {
  juce::String status;
  status << juce::String(TRANS("%n tracks")).replace("%n", juce::String(displayedTracks_.size()));
  if (displayedTracks_.size() != allTracks_.size()) {
    status << " " << juce::String(TRANS("(filtered from %n)")).replace("%n", juce::String(allTracks_.size()));
  }
  if (scan_.scanning) {
    status << " | " << TRANS("scanning") << " " << static_cast<int>(scan_.processed) << "/"
           << static_cast<int>(scan_.discovered);
  } else if (scan_.analysisPending > 0) {
    status << " | "
           << juce::String(TRANS("preparing: %n tasks left")).replace("%n", juce::String(scan_.analysisPending));
    if (pendingHistory_.size() >= 2) {
      const auto& oldest = pendingHistory_.front();
      const auto& newest = pendingHistory_.back();
      const double seconds = std::chrono::duration<double>(newest.at - oldest.at).count();
      const double done = static_cast<double>(oldest.pending) - static_cast<double>(newest.pending);
      if (seconds > 5.0 && done > 0.0) {
        const int remaining = static_cast<int>(static_cast<double>(newest.pending) / (done / seconds));
        status << " (~" << (remaining >= 90 ? juce::String((remaining + 30) / 60) + " " + TRANS("min") : juce::String(remaining) + " " + TRANS("s"))
               << ")";
      }
    }
  } else if (!scan_.lastError.empty()) {
    status << " | " << TRANS("scan failed") << ": " << scan_.lastError;
  }
  statusLabel_.setText(status, juce::dontSendNotification);
}

void LibraryComponent::updateScanStatus() {
  if (!source_) {
    return;
  }
  const core::LibraryScanStatus latest = source_->scanStatus();
  const bool changed = latest.processed != scan_.processed || latest.scanning != scan_.scanning ||
                       latest.analysisPending != scan_.analysisPending;
  scan_ = latest;

  // Keep about a minute of samples for the time-left estimate; a rising count (new work) restarts it.
  const auto now = std::chrono::steady_clock::now();
  if (!pendingHistory_.empty() && latest.analysisPending > pendingHistory_.back().pending) {
    pendingHistory_.clear();
  }
  pendingHistory_.push_back({now, latest.analysisPending});
  while (pendingHistory_.size() > 1 && now - pendingHistory_.front().at > std::chrono::seconds(60)) {
    pendingHistory_.pop_front();
  }
  if (changed) {
    refreshTracks();  // new rows appeared (or the scan finished): reload the table and the status text
  }
}

void LibraryComponent::setFourDecks(bool fourDecks) {
  fourDecks_ = fourDecks;
  loadDeckCButton_.setVisible(fourDecks);
  loadDeckDButton_.setVisible(fourDecks);
  resized();
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
                case ColStatus: {
                  const double ra = a.analysisTotal > 0 ? static_cast<double>(a.analysisDone) / a.analysisTotal : 0.0;
                  const double rb = b.analysisTotal > 0 ? static_cast<double>(b.analysisDone) / b.analysisTotal : 0.0;
                  cmp = (ra < rb) ? -1 : ((ra > rb) ? 1 : 0);
                  break;
                }
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
      g.drawText(track.title.empty() ? TRANS("(Untitled)") : track.title, cellBounds, juce::Justification::centredLeft, true);
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
    case ColStatus: {
      // How far the background preparation (tempo, grid, key, energy, structure) has got for this track.
      const auto bar = juce::Rectangle<float>(4.0f, 4.0f, static_cast<float>(width) - 8.0f, static_cast<float>(height) - 8.0f);
      if (track.analysisTotal <= 0) {
        g.setColour(theme_.textDim);
        g.drawText("--", cellBounds, juce::Justification::centred, true);
      } else if (track.analysisDone >= track.analysisTotal) {
        g.setColour(track.analysisFailed > 0 ? theme_.meterRed.withAlpha(0.9f) : theme_.playActive);
        g.drawText(track.analysisFailed > 0 ? juce::String(TRANS("Ready (%n failed)")).replace("%n", juce::String(track.analysisFailed))
                                            : juce::String(TRANS("Ready")),
                   cellBounds, juce::Justification::centred, true);
      } else {
        const float fraction = static_cast<float>(track.analysisDone) / static_cast<float>(track.analysisTotal);
        g.setColour(theme_.panel.brighter(0.15f));
        g.fillRoundedRectangle(bar, 3.0f);
        g.setColour(theme_.accent.withAlpha(0.55f));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * fraction), 3.0f);
        g.setColour(theme_.text);
        g.drawText(juce::String(TRANS("Preparing")) + " " + juce::String(track.analysisDone) + "/" + juce::String(track.analysisTotal), cellBounds,
                   juce::Justification::centred, true);
      }
      break;
    }
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

void LibraryComponent::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) {
  juce::ignoreUnused(columnId);
  if (e.mods.isPopupMenu() && rowNumber >= 0 && rowNumber < static_cast<int>(displayedTracks_.size())) {
    table_.selectRow(rowNumber);
    showTrackMenu(rowNumber);
  }
}

void LibraryComponent::showTrackMenu(int row) {
  const core::TrackItem track = displayedTracks_[static_cast<std::size_t>(row)];
  const bool prepared = track.analysisTotal > 0 && track.analysisDone >= track.analysisTotal && track.analysisFailed == 0;

  juce::PopupMenu menu;
  menu.addSectionHeader(track.title.empty() ? juce::String(TRANS("(Untitled)")) : juce::String(track.title));
  menu.addItem(1, TRANS("Load to deck A"));
  menu.addItem(2, TRANS("Load to deck B"));
  if (fourDecks_) {
    menu.addItem(3, TRANS("Load to deck C"));
    menu.addItem(4, TRANS("Load to deck D"));
  }
  menu.addSeparator();
  menu.addItem(10, TRANS("Mix next (live)"), onMixNextRequested != nullptr);
  menu.addSeparator();
  menu.addItem(20, prepared ? TRANS("Analyse now (already prepared)") : TRANS("Analyse now (prepare this track first)"), source_ != nullptr && !prepared);
  menu.addItem(21, TRANS("Re-analyse with the AI (tempo, key, markers)"), source_ != nullptr);
  menu.addSeparator();
  menu.addItem(30, TRANS("Show in file manager"));
  menu.addItem(31, TRANS("Copy file path"));
  menu.addSeparator();
  menu.addItem(40, TRANS("Remove from library..."), source_ != nullptr);

  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&table_), [this, track](int choice) {
    const juce::File file(juce::String::fromUTF8(track.filepath.c_str()));
    switch (choice) {
      case 1: loadTrackToDeck(track, core::DeckId::A); break;
      case 2: loadTrackToDeck(track, core::DeckId::B); break;
      case 3: loadTrackToDeck(track, core::DeckId::C); break;
      case 4: loadTrackToDeck(track, core::DeckId::D); break;
      case 10:
        if (onMixNextRequested) onMixNextRequested(track);
        break;
      case 20:
        source_->prioritizeAnalysis(track.id);
        refreshTracks();
        break;
      case 21:
        source_->reanalyze(track.id);
        refreshTracks();
        break;
      case 30:
        file.revealToUser();
        break;
      case 31:
        juce::SystemClipboard::copyTextToClipboard(file.getFullPathName());
        break;
      case 40:
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon, TRANS("Remove from library"),
            juce::String(TRANS("Remove \"%t\" from the library?\nThe file on disk is not touched."))
                .replace("%t", juce::String(track.title)),
            TRANS("Remove"),
            TRANS("Cancel"), this, juce::ModalCallbackFunction::create([this, id = track.id](int result) {
              if (result == 1 && source_) {
                source_->removeTrack(id);
                refreshTracks();
              }
            }));
        break;
      default:
        break;
    }
  });
}

void LibraryComponent::loadTrackToDeck(const core::TrackItem& track, core::DeckId deck) {
  if (onTrackLoadRequested) {
    onTrackLoadRequested(track, deck);
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
  topBar.removeFromLeft(4);
  mixNextButton_.setBounds(topBar.removeFromLeft(80));
  topBar.removeFromLeft(4);
  if (fourDecks_) {
    loadDeckCButton_.setBounds(topBar.removeFromLeft(70));
    topBar.removeFromLeft(4);
    loadDeckDButton_.setBounds(topBar.removeFromLeft(70));
    topBar.removeFromLeft(4);
  }
  topBar.removeFromLeft(8);

  statusLabel_.setBounds(topBar.removeFromRight(300));
  searchBox_.setBounds(topBar);

  bounds.removeFromTop(6);
  table_.setBounds(bounds);
}

}  // namespace zyron::ui
