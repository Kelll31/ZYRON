// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Database/SqliteLibrarySource.hpp"

#include <utility>

#include "Library/Database/LibraryRepository.hpp"

namespace zyron::library {

namespace {

core::TrackItem toTrackItem(const TrackRecord& rec) {
  core::TrackItem item;
  item.id = rec.id;
  item.filepath = rec.filepath;
  item.contentHash = rec.contentHash;
  item.title = rec.title;
  item.artist = rec.artist;
  item.album = rec.album;
  item.genre = rec.genre;
  item.year = rec.year;
  item.bpm = rec.bpm;
  item.key = rec.key;
  item.energy = rec.energy;
  item.loudnessLufs = rec.loudnessLufs;
  item.durationSec = rec.durationSec;
  item.waveformPeaksPath = rec.waveformPeaksPath;
  item.stemStatus = rec.stemStatus;
  return item;
}

}  // namespace

SqliteLibrarySource::SqliteLibrarySource(std::shared_ptr<Database> db,
                                         std::shared_ptr<LibraryScanner> scanner)
    : db_(std::move(db)), scanner_(std::move(scanner)) {}

std::vector<core::TrackItem> SqliteLibrarySource::search(std::string_view query) {
  if (!db_) return {};
  if (query.empty()) {
    return listAll();
  }

  // FTS5 query search
  const auto records = LibraryRepository::searchTracks(*db_, query);
  std::vector<core::TrackItem> items;
  items.reserve(records.size());
  for (const auto& rec : records) {
    items.push_back(toTrackItem(rec));
  }
  return items;
}

std::vector<core::TrackItem> SqliteLibrarySource::listAll() {
  if (!db_) return {};
  const auto records = LibraryRepository::listAllTracks(*db_);
  std::vector<core::TrackItem> items;
  items.reserve(records.size());
  for (const auto& rec : records) {
    items.push_back(toTrackItem(rec));
  }
  return items;
}

void SqliteLibrarySource::requestScan(const std::string& folderPath) {
  if (scanner_ && db_ && !folderPath.empty()) {
    scanner_->scanDirectorySync(folderPath, *db_);
  }
}

}  // namespace zyron::library
