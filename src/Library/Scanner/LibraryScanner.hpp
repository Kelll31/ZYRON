// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Library/Database/Database.hpp"
#include "Library/Database/DatabaseWriter.hpp"
#include "Library/Database/LibraryRepository.hpp"
#include "Library/Hash/ContentHasher.hpp"
#include "Library/Metadata/MetadataExtractor.hpp"

namespace zyron::library {

struct ScanOptions {
  bool recursive{true};
  std::vector<std::string> extensions{
      ".wav", ".mp3", ".flac", ".ogg", ".aiff", ".aif", ".m4a", ".aac"};
  bool forceRescan{false};          // re-examine and re-hash even if mtime & size match
  bool enqueueAnalysisTasks{true};  // automatically enqueue initial analysis tasks
};

struct ScanStatistics {
  std::size_t totalDiscovered{0};
  std::size_t processed{0};
  std::size_t newlyAdded{0};
  std::size_t updated{0};
  std::size_t skipped{0};
  std::size_t errors{0};
  std::filesystem::path currentFile;
  bool isComplete{false};
};

using ScanProgressCallback = std::function<void(const ScanStatistics&)>;

/// Background audio library folder scanner (SPEC section 29, ARCHITECTURE section 9).
///
/// Features:
///  - Incremental scanning: skips unchanged files via (mtime, fileSize) comparison.
///  - Content-hash identity: tracks moved or renamed files and preserves existing cues, grids, and stems.
///  - Audio metadata extraction: parses ID3/Vorbis/RIFF tags or derives from filename.
///  - Non-blocking background worker thread with cancellation and progress reporting.
///  - Directly callable synchronously or asynchronously via DatabaseWriter.
class LibraryScanner {
 public:
  LibraryScanner();
  explicit LibraryScanner(std::unique_ptr<MetadataExtractor> extractor);
  ~LibraryScanner();

  LibraryScanner(const LibraryScanner&) = delete;
  LibraryScanner& operator=(const LibraryScanner&) = delete;

  /// Synchronously scans a directory against a direct Database connection.
  ScanStatistics scanDirectorySync(const std::filesystem::path& folder,
                                   Database& db,
                                   const ScanOptions& options = {},
                                   ScanProgressCallback progressCb = nullptr);

  /// Synchronously scans a directory routing database writes through DatabaseWriter.
  ScanStatistics scanDirectorySync(const std::filesystem::path& folder,
                                   DatabaseWriter& writer,
                                   Database& readerDb,
                                   const ScanOptions& options = {},
                                   ScanProgressCallback progressCb = nullptr);

  /// Asynchronously launches a background scan thread.
  void startScanAsync(const std::filesystem::path& folder,
                      DatabaseWriter& writer,
                      const std::filesystem::path& dbPathForReader,
                      const ScanOptions& options = {},
                      ScanProgressCallback progressCb = nullptr);

  /// Requests cancellation of any active background scan and waits for worker termination.
  void stopScan();

  /// Returns whether a background scan is currently in progress.
  [[nodiscard]] bool isScanning() const noexcept;

  /// Returns current snapshot of statistics.
  [[nodiscard]] ScanStatistics currentStatistics() const;

 private:
  void workerLoop(std::filesystem::path folder,
                  DatabaseWriter* writer,
                  std::filesystem::path dbPathForReader,
                  ScanOptions options,
                  ScanProgressCallback progressCb);

  bool processSingleFile(const std::filesystem::path& path,
                         Database& db,
                         const ScanOptions& options,
                         ScanStatistics& stats);

  std::unique_ptr<MetadataExtractor> extractor_;
  std::atomic<bool> cancelRequested_{false};
  std::atomic<bool> isScanning_{false};
  mutable std::mutex statsMutex_;
  ScanStatistics stats_;
  std::thread workerThread_;
};

}  // namespace zyron::library
