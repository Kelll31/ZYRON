// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Scanner/LibraryScanner.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace zyron::library {

namespace {

std::string pathToUtf8(const std::filesystem::path& p) {
  const auto u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string toLower(std::string_view s) {
  std::string result(s);
  for (char& c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

std::int64_t toEpochSeconds(std::filesystem::file_time_type ftime) {
  return std::chrono::duration_cast<std::chrono::seconds>(ftime.time_since_epoch()).count();
}

bool hasSupportedExtension(const std::filesystem::path& path, const std::vector<std::string>& extensions) {
  const std::string ext = toLower(path.extension().string());
  return std::any_of(extensions.begin(), extensions.end(), [&](const std::string& candidate) {
    return ext == toLower(candidate);
  });
}

}  // namespace

LibraryScanner::LibraryScanner()
    : extractor_(std::make_unique<MetadataExtractor>()) {}

LibraryScanner::LibraryScanner(std::unique_ptr<MetadataExtractor> extractor)
    : extractor_(std::move(extractor)) {
  if (!extractor_) {
    extractor_ = std::make_unique<MetadataExtractor>();
  }
}

LibraryScanner::~LibraryScanner() {
  stopScan();
}

bool LibraryScanner::processSingleFile(const std::filesystem::path& path,
                                       Database& db,
                                       const ScanOptions& options,
                                       ScanStatistics& stats) {
  std::error_code ec;
  const auto size = static_cast<std::int64_t>(std::filesystem::file_size(path, ec));
  if (ec) {
    stats.errors++;
    return false;
  }

  const auto ftime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    stats.errors++;
    return false;
  }
  const std::int64_t mtime = toEpochSeconds(ftime);

  // 1. Check if track with this path is already indexed
  auto existingByPath = LibraryRepository::findTrackByPath(db, path);
  if (existingByPath.has_value() && !options.forceRescan) {
    if (existingByPath->fileSize == size && existingByPath->fileMtime == mtime) {
      stats.skipped++;
      return true;
    }
  }

  // 2. Compute SHA-256 content hash
  std::string hashErr;
  const std::string contentHash = ContentHasher::hashFile(path, &hashErr);
  if (contentHash.empty()) {
    stats.errors++;
    return false;
  }

  // 3. Check for moved / renamed file by content hash
  if (!existingByPath.has_value()) {
    auto existingByHash = LibraryRepository::findTrackByContentHash(db, contentHash);
    if (existingByHash.has_value()) {
      // Retain cues, beatgrids, and stems while updating location
      existingByHash->filepath = pathToUtf8(path);
      existingByHash->fileSize = size;
      existingByHash->fileMtime = mtime;
      LibraryRepository::updateTrack(db, *existingByHash);
      stats.updated++;
      return true;
    }
  }

  // 4. Extract metadata
  TrackMetadata meta;
  (void)extractor_->extract(path, meta);

  if (existingByPath.has_value()) {
    // Modified in place
    auto updated = *existingByPath;
    updated.contentHash = contentHash;
    updated.fileSize = size;
    updated.fileMtime = mtime;
    updated.title = meta.title;
    updated.artist = meta.artist;
    updated.album = meta.album;
    updated.genre = meta.genre;
    updated.year = meta.year;
    updated.durationSec = meta.durationSec;
    updated.sampleRate = meta.sampleRate;
    updated.channels = meta.channels;
    LibraryRepository::updateTrack(db, updated);
    stats.updated++;
  } else {
    // Brand new track
    TrackRecord record;
    record.filepath = pathToUtf8(path);
    record.contentHash = contentHash;
    record.fileSize = size;
    record.fileMtime = mtime;
    record.title = meta.title;
    record.artist = meta.artist;
    record.album = meta.album;
    record.genre = meta.genre;
    record.year = meta.year;
    record.durationSec = meta.durationSec;
    record.sampleRate = meta.sampleRate;
    record.channels = meta.channels;

    const auto trackId = LibraryRepository::insertTrack(db, record);
    if (options.enqueueAnalysisTasks && trackId > 0) {
      LibraryRepository::enqueueAnalysisTask(db, trackId, "duration", 1);
      LibraryRepository::enqueueAnalysisTask(db, trackId, "waveform", 1);
      LibraryRepository::enqueueAnalysisTask(db, trackId, "bpm", 1);
      LibraryRepository::enqueueAnalysisTask(db, trackId, "beatgrid", 1);
      LibraryRepository::enqueueAnalysisTask(db, trackId, "key", 1);
      LibraryRepository::enqueueAnalysisTask(db, trackId, "energy", 1);
    }
    stats.newlyAdded++;
  }

  return true;
}

ScanStatistics LibraryScanner::scanDirectorySync(const std::filesystem::path& folder,
                                                 Database& db,
                                                 const ScanOptions& options,
                                                 ScanProgressCallback progressCb) {
  ScanStatistics stats;
  std::error_code ec;

  if (!std::filesystem::exists(folder, ec) || !std::filesystem::is_directory(folder, ec)) {
    stats.isComplete = true;
    if (progressCb) progressCb(stats);
    return stats;
  }

  // Discover candidate files
  std::vector<std::filesystem::path> files;
  if (options.recursive) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
      if (cancelRequested_.load(std::memory_order_relaxed)) break;
      if (entry.is_regular_file(ec)) {
        const auto filename = entry.path().filename().string();
        if (!filename.empty() && filename.front() != '.' && hasSupportedExtension(entry.path(), options.extensions)) {
          files.push_back(entry.path());
        }
      }
    }
  } else {
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
      if (cancelRequested_.load(std::memory_order_relaxed)) break;
      if (entry.is_regular_file(ec)) {
        const auto filename = entry.path().filename().string();
        if (!filename.empty() && filename.front() != '.' && hasSupportedExtension(entry.path(), options.extensions)) {
          files.push_back(entry.path());
        }
      }
    }
  }

  stats.totalDiscovered = files.size();

  // Process files inside a transaction for performance
  constexpr std::size_t kBatchSize = 64;
  std::size_t inBatch = 0;
  db.beginTransaction();

  for (const auto& path : files) {
    if (cancelRequested_.load(std::memory_order_relaxed)) break;

    stats.currentFile = path;
    processSingleFile(path, db, options, stats);
    stats.processed++;
    inBatch++;

    if (inBatch >= kBatchSize) {
      db.commit();
      db.beginTransaction();
      inBatch = 0;
    }

    if (progressCb) {
      progressCb(stats);
    }
  }

  if (inBatch > 0) {
    db.commit();
  } else {
    db.commit();  // Commit the open empty transaction
  }

  stats.isComplete = true;
  stats.currentFile.clear();

  {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = stats;
  }

  if (progressCb) {
    progressCb(stats);
  }

  return stats;
}

ScanStatistics LibraryScanner::scanDirectorySync(const std::filesystem::path& folder,
                                                 DatabaseWriter& writer,
                                                 Database& readerDb,
                                                 const ScanOptions& options,
                                                 ScanProgressCallback progressCb) {
  // Discover candidate files
  ScanStatistics stats;
  std::error_code ec;

  if (!std::filesystem::exists(folder, ec) || !std::filesystem::is_directory(folder, ec)) {
    stats.isComplete = true;
    if (progressCb) progressCb(stats);
    return stats;
  }

  std::vector<std::filesystem::path> files;
  if (options.recursive) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
      if (cancelRequested_.load(std::memory_order_relaxed)) break;
      if (entry.is_regular_file(ec)) {
        const auto filename = entry.path().filename().string();
        if (!filename.empty() && filename.front() != '.' && hasSupportedExtension(entry.path(), options.extensions)) {
          files.push_back(entry.path());
        }
      }
    }
  } else {
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
      if (cancelRequested_.load(std::memory_order_relaxed)) break;
      if (entry.is_regular_file(ec)) {
        const auto filename = entry.path().filename().string();
        if (!filename.empty() && filename.front() != '.' && hasSupportedExtension(entry.path(), options.extensions)) {
          files.push_back(entry.path());
        }
      }
    }
  }

  stats.totalDiscovered = files.size();

  for (const auto& path : files) {
    if (cancelRequested_.load(std::memory_order_relaxed)) break;

    stats.currentFile = path;
    const auto size = static_cast<std::int64_t>(std::filesystem::file_size(path, ec));
    const auto ftime = std::filesystem::last_write_time(path, ec);
    const std::int64_t mtime = toEpochSeconds(ftime);

    auto existingByPath = LibraryRepository::findTrackByPath(readerDb, path);
    if (existingByPath.has_value() && !options.forceRescan) {
      if (existingByPath->fileSize == size && existingByPath->fileMtime == mtime) {
        stats.skipped++;
        stats.processed++;
        if (progressCb) progressCb(stats);
        continue;
      }
    }

    std::string hashErr;
    const std::string contentHash = ContentHasher::hashFile(path, &hashErr);
    if (contentHash.empty()) {
      stats.errors++;
      stats.processed++;
      if (progressCb) progressCb(stats);
      continue;
    }

    if (!existingByPath.has_value()) {
      auto existingByHash = LibraryRepository::findTrackByContentHash(readerDb, contentHash);
      if (existingByHash.has_value()) {
        auto updated = *existingByHash;
        updated.filepath = pathToUtf8(path);
        updated.fileSize = size;
        updated.fileMtime = mtime;
        writer.post([updated](Database& db) {
          LibraryRepository::updateTrack(db, updated);
        }).get();
        stats.updated++;
        stats.processed++;
        if (progressCb) progressCb(stats);
        continue;
      }
    }

    TrackMetadata meta;
    (void)extractor_->extract(path, meta);

    if (existingByPath.has_value()) {
      auto updated = *existingByPath;
      updated.contentHash = contentHash;
      updated.fileSize = size;
      updated.fileMtime = mtime;
      updated.title = meta.title;
      updated.artist = meta.artist;
      updated.album = meta.album;
      updated.genre = meta.genre;
      updated.year = meta.year;
      updated.durationSec = meta.durationSec;
      updated.sampleRate = meta.sampleRate;
      updated.channels = meta.channels;
      writer.post([updated](Database& db) {
        LibraryRepository::updateTrack(db, updated);
      }).get();
      stats.updated++;
    } else {
      TrackRecord record;
      record.filepath = pathToUtf8(path);
      record.contentHash = contentHash;
      record.fileSize = size;
      record.fileMtime = mtime;
      record.title = meta.title;
      record.artist = meta.artist;
      record.album = meta.album;
      record.genre = meta.genre;
      record.year = meta.year;
      record.durationSec = meta.durationSec;
      record.sampleRate = meta.sampleRate;
      record.channels = meta.channels;

      const bool enqueue = options.enqueueAnalysisTasks;
      writer.post([record, enqueue](Database& db) {
        const auto trackId = LibraryRepository::insertTrack(db, record);
        if (enqueue && trackId > 0) {
          LibraryRepository::enqueueAnalysisTask(db, trackId, "duration", 1);
          LibraryRepository::enqueueAnalysisTask(db, trackId, "waveform", 1);
          LibraryRepository::enqueueAnalysisTask(db, trackId, "bpm", 1);
          LibraryRepository::enqueueAnalysisTask(db, trackId, "beatgrid", 1);
          LibraryRepository::enqueueAnalysisTask(db, trackId, "key", 1);
          LibraryRepository::enqueueAnalysisTask(db, trackId, "energy", 1);
        }
      }).get();
      stats.newlyAdded++;
    }

    stats.processed++;
    if (progressCb) progressCb(stats);
  }

  stats.isComplete = true;
  stats.currentFile.clear();

  {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = stats;
  }

  if (progressCb) progressCb(stats);
  return stats;
}

void LibraryScanner::startScanAsync(const std::filesystem::path& folder,
                                    DatabaseWriter& writer,
                                    const std::filesystem::path& dbPathForReader,
                                    const ScanOptions& options,
                                    ScanProgressCallback progressCb) {
  stopScan();
  cancelRequested_.store(false, std::memory_order_release);
  isScanning_.store(true, std::memory_order_release);

  workerThread_ = std::thread(&LibraryScanner::workerLoop, this, folder, &writer, dbPathForReader, options, progressCb);
}

void LibraryScanner::workerLoop(std::filesystem::path folder,
                                DatabaseWriter* writer,
                                std::filesystem::path dbPathForReader,
                                ScanOptions options,
                                ScanProgressCallback progressCb) {
  try {
    Database readerDb = Database::open(dbPathForReader);
    scanDirectorySync(folder, *writer, readerDb, options, progressCb);
  } catch (...) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.errors++;
    stats_.isComplete = true;
  }
  isScanning_.store(false, std::memory_order_release);
}

void LibraryScanner::stopScan() {
  cancelRequested_.store(true, std::memory_order_release);
  if (workerThread_.joinable()) {
    workerThread_.join();
  }
  isScanning_.store(false, std::memory_order_release);
}

bool LibraryScanner::isScanning() const noexcept {
  return isScanning_.load(std::memory_order_acquire);
}

ScanStatistics LibraryScanner::currentStatistics() const {
  std::lock_guard<std::mutex> lock(statsMutex_);
  return stats_;
}

}  // namespace zyron::library
