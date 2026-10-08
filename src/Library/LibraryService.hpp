// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Core/Library/LibraryTypes.hpp"
#include "Library/Analysis/AnalysisTaskQueue.hpp"
#include "Library/Database/Database.hpp"
#include "Library/Scanner/LibraryScanner.hpp"

namespace zyron::library {

/// The library as the application uses it: one SQLite file, a reader connection for the UI and the engine, and a
/// single background worker that scans folders on its own connection (WAL allows one writer next to readers).
/// Nothing here blocks the caller for longer than a query (SPEC sections 28-30, ARCHITECTURE section 9).
///
/// The scanned folders are remembered in `foldersFile` (one UTF-8 path per line) so the next start can rescan them
/// incrementally.
class LibraryService final : public core::ILibrarySource {
 public:
  /// Opens (and migrates) the database. Throws std::runtime_error if it cannot be opened.
  LibraryService(std::filesystem::path dbPath, std::filesystem::path foldersFile);
  ~LibraryService() override;

  LibraryService(const LibraryService&) = delete;
  LibraryService& operator=(const LibraryService&) = delete;

  // core::ILibrarySource
  [[nodiscard]] std::vector<core::TrackItem> search(std::string_view query) override;
  [[nodiscard]] std::vector<core::TrackItem> listAll() override;
  void requestScan(const std::string& folderPath) override;
  [[nodiscard]] std::optional<core::TrackItem> findTrack(std::int64_t id) override;
  [[nodiscard]] core::LibraryScanStatus scanStatus() const override;

  /// Registers a handler for one analysis task type ("bpm", "key", ...). Call before startAnalysis().
  void registerAnalysisHandler(std::string_view taskType, TaskHandler handler, int version);
  /// Starts the background worker that runs the queued analysis tasks (the scanner queues them for new tracks).
  void startAnalysis(const std::filesystem::path& cacheDir);

  // Track markers: mix points, drops, breakdowns (cue points from index kFirstMarkerIndex up).
  [[nodiscard]] std::vector<core::TrackMarker> markers(std::int64_t trackId) override;
  int setMarker(std::int64_t trackId, const core::TrackMarker& marker) override;
  void removeMarker(std::int64_t trackId, int markerId) override;
  /// The encoded per-bar energy profile of a track (analysis::BarProfile::encode()); empty when not analysed yet.
  [[nodiscard]] std::string barProfile(std::int64_t trackId);
  void prioritizeAnalysis(std::int64_t trackId) override;
  void reanalyze(std::int64_t trackId) override;
  void removeTrack(std::int64_t trackId) override;
  /// Cue indices below this belong to the hot cues; markers use the range above.
  static constexpr int kFirstMarkerIndex = 100;

  /// Folders added so far (persisted). Used at start-up to refresh the library.
  [[nodiscard]] std::vector<std::string> knownFolders() const;
  /// Queues every known folder for an incremental scan.
  void rescanKnownFolders();

 private:
  void workerLoop();
  void rememberFolder(const std::string& folder);
  void ensureAnalysisTasks();  // queues the registered task types for tracks that do not have them yet
  void setStatus(const core::LibraryScanStatus& status);

  std::filesystem::path dbPath_;
  std::filesystem::path foldersFile_;

  mutable std::mutex readMutex_;
  Database readDb_;

  mutable std::mutex queueMutex_;
  std::condition_variable queueCv_;
  std::deque<std::string> pending_;
  std::vector<std::string> folders_;
  bool stopping_{false};

  mutable std::mutex statusMutex_;
  core::LibraryScanStatus status_;

  LibraryScanner scanner_;
  std::vector<std::string> analysisTypes_;  // handler types registered so far
  std::unique_ptr<DatabaseWriter> analysisWriter_;
  AnalysisTaskQueue analysis_;
  std::thread worker_;
};

}  // namespace zyron::library
