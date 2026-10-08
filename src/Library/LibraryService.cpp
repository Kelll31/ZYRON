// SPDX-License-Identifier: AGPL-3.0-only
#include <chrono>
#include "Library/LibraryService.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include "Library/Database/LibraryRepository.hpp"
#include "Library/Database/Migrations.hpp"

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

std::vector<std::string> readFolders(const std::filesystem::path& file) {
  std::vector<std::string> folders;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    if (line.ends_with("\r")) {
      line.pop_back();
    }
    if (!line.empty()) {
      folders.push_back(line);
    }
  }
  return folders;
}

Database openMigrated(const std::filesystem::path& path) {
  Database db = Database::open(path);
  Migrations::apply(db);
  return db;
}

bool passesQuickCheck(Database& db) {
  try {
    auto check = db.prepare("PRAGMA quick_check;");
    return check.step() && check.getText(0) == "ok";
  } catch (const std::exception&) {
    return false;
  }
}

/// Opens the library at start-up. A damaged file (a crash or a killed process mid-write can do that) is not fatal: it
/// is moved aside, never deleted, and a fresh library is started. The folders are scanned again and each track's
/// analysis comes back from the analysis file in its music folder, so nothing has to be recomputed.
Database openOrRecover(const std::filesystem::path& path) {
  try {
    Database db = Database::open(path);
    if (passesQuickCheck(db)) {
      Migrations::apply(db);
      return db;
    }
  } catch (const std::exception&) {
    // falls through to the recovery below
  }
  const auto stamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
  for (const char* suffix : {"", "-wal", "-shm"}) {
    std::filesystem::path file = path;
    file += suffix;
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
      std::filesystem::path aside = path;
      aside += ".corrupt-" + stamp + suffix;
      std::filesystem::rename(file, aside, ec);
    }
  }
  return openMigrated(path);
}

}  // namespace

LibraryService::LibraryService(std::filesystem::path dbPath, std::filesystem::path foldersFile)
    : dbPath_(std::move(dbPath)),
      foldersFile_(std::move(foldersFile)),
      readDb_(openOrRecover(dbPath_)),
      folders_(readFolders(foldersFile_)) {
  worker_ = std::thread(&LibraryService::workerLoop, this);
}

LibraryService::~LibraryService() {
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    stopping_ = true;
  }
  scanner_.stopScan();
  analysis_.stopWorker();
  queueCv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

namespace {

const char* markerColor(const std::string& type) {
  if (type == core::TrackMarker::kMixIn) return "#00FF88";
  if (type == core::TrackMarker::kMixOut) return "#FFCC00";
  if (type == core::TrackMarker::kDrop) return "#FF3366";
  if (type == core::TrackMarker::kBreak) return "#3399FF";
  if (type == core::TrackMarker::kIntro) return "#66D9A8";
  if (type == core::TrackMarker::kOutro) return "#FFAA33";
  return "#00D2FF";
}

}  // namespace

std::vector<core::TrackMarker> LibraryService::markers(std::int64_t trackId) {
  std::lock_guard<std::mutex> lock(readMutex_);
  const auto track = LibraryRepository::findTrackById(readDb_, trackId);
  std::vector<core::TrackMarker> result;
  if (!track.has_value() || track->sampleRate <= 0) {
    return result;
  }
  for (const auto& cue : LibraryRepository::getCuePoints(readDb_, trackId)) {
    if (cue.index < kFirstMarkerIndex) {
      continue;
    }
    core::TrackMarker marker;
    marker.id = cue.index;
    marker.timeSec = static_cast<double>(cue.frame) / static_cast<double>(track->sampleRate);
    marker.type = cue.type;
    marker.name = cue.name;
    marker.source = cue.source;
    result.push_back(std::move(marker));
  }
  std::sort(result.begin(), result.end(),
            [](const core::TrackMarker& a, const core::TrackMarker& b) { return a.timeSec < b.timeSec; });
  return result;
}

int LibraryService::setMarker(std::int64_t trackId, const core::TrackMarker& marker) {
  std::lock_guard<std::mutex> lock(readMutex_);
  const auto track = LibraryRepository::findTrackById(readDb_, trackId);
  if (!track.has_value() || track->sampleRate <= 0 || marker.timeSec < 0.0) {
    return 0;
  }
  int index = marker.id;
  if (index < kFirstMarkerIndex) {
    index = kFirstMarkerIndex;
    for (const auto& cue : LibraryRepository::getCuePoints(readDb_, trackId)) {
      index = std::max(index, cue.index + 1);
    }
  }
  CuePointRecord cue;
  cue.trackId = trackId;
  cue.index = index;
  cue.frame = static_cast<std::int64_t>(marker.timeSec * static_cast<double>(track->sampleRate));
  cue.name = marker.name;
  cue.type = marker.type;
  cue.color = markerColor(marker.type);
  cue.source = "user";
  LibraryRepository::saveCuePoint(readDb_, cue);
  return index;
}

void LibraryService::removeMarker(std::int64_t trackId, int markerId) {
  if (markerId < kFirstMarkerIndex) {
    return;
  }
  std::lock_guard<std::mutex> lock(readMutex_);
  LibraryRepository::removeCuePoint(readDb_, trackId, markerId);
}

std::string LibraryService::barProfile(std::int64_t trackId) {
  std::lock_guard<std::mutex> lock(readMutex_);
  const auto track = LibraryRepository::findTrackById(readDb_, trackId);
  return track.has_value() ? track->barProfile : std::string{};
}

void LibraryService::prioritizeAnalysis(std::int64_t trackId) {
  std::lock_guard<std::mutex> lock(readMutex_);
  try {
    auto stmt = readDb_.prepare(
        "UPDATE analysis_tasks SET priority = 1, status = 'pending', error_message = '' "
        "WHERE track_id = ? AND status IN ('pending', 'failed');");
    stmt.bindInt64(1, trackId);
    stmt.execute();
  } catch (const std::exception&) {
    // A busy database must not break the menu: the tasks simply keep their place in the queue.
  }
}

void LibraryService::reanalyze(std::int64_t trackId) {
  std::lock_guard<std::mutex> lock(readMutex_);
  try {
    auto stmt = readDb_.prepare(
        "UPDATE analysis_tasks SET priority = 1, status = 'pending', error_message = '' WHERE track_id = ?;");
    stmt.bindInt64(1, trackId);
    stmt.execute();
  } catch (const std::exception&) {
  }
}

void LibraryService::removeTrack(std::int64_t trackId) {
  std::lock_guard<std::mutex> lock(readMutex_);
  try {
    LibraryRepository::deleteTrack(readDb_, trackId);
  } catch (const std::exception&) {
  }
}

void LibraryService::ensureAnalysisTasks() {
  try {
    Database db = Database::open(dbPath_);
    for (const std::string& type : analysisTypes_) {
      auto stmt = db.prepare(
          "INSERT INTO analysis_tasks (track_id, task_type, status, version) SELECT id, ?, 'pending', 1 FROM tracks "
          "WHERE true ON CONFLICT(track_id, task_type) DO NOTHING;");
      stmt.bindText(1, type);
      stmt.execute();
    }
  } catch (const std::exception&) {
    // Not fatal: tracks without tasks simply stay unanalysed until the next scan.
  }
}

std::vector<core::TrackItem> LibraryService::search(std::string_view query) {
  if (query.empty()) {
    return listAll();
  }
  std::lock_guard<std::mutex> lock(readMutex_);
  std::vector<core::TrackItem> items;
  for (const auto& rec : LibraryRepository::searchTracks(readDb_, query)) {
    items.push_back(toTrackItem(rec));
  }
  return items;
}

namespace {

struct TaskCounts {
  int total{0};
  int finished{0};
  int failed{0};
};

/// Task counts per track in one query (a row per track and status).
std::unordered_map<std::int64_t, TaskCounts> taskCounts(Database& db) {
  std::unordered_map<std::int64_t, TaskCounts> counts;
  auto stmt = db.prepare("SELECT track_id, status, COUNT(*) FROM analysis_tasks GROUP BY track_id, status;");
  while (stmt.step()) {
    TaskCounts& entry = counts[stmt.getInt64(0)];
    const std::string status = stmt.getText(1);
    const int count = stmt.getInt(2);
    entry.total += count;
    if (status == "completed") {
      entry.finished += count;
    } else if (status == "failed") {
      entry.finished += count;
      entry.failed += count;
    }
  }
  return counts;
}

}  // namespace

std::vector<core::TrackItem> LibraryService::listAll() {
  std::lock_guard<std::mutex> lock(readMutex_);
  const auto counts = taskCounts(readDb_);
  std::vector<core::TrackItem> items;
  for (const auto& rec : LibraryRepository::listAllTracks(readDb_)) {
    core::TrackItem item = toTrackItem(rec);
    if (const auto found = counts.find(rec.id); found != counts.end()) {
      item.analysisTotal = found->second.total;
      item.analysisDone = found->second.finished;
      item.analysisFailed = found->second.failed;
    }
    items.push_back(std::move(item));
  }
  return items;
}

std::optional<core::TrackItem> LibraryService::findTrack(std::int64_t id) {
  std::lock_guard<std::mutex> lock(readMutex_);
  const auto record = LibraryRepository::findTrackById(readDb_, id);
  if (!record.has_value()) {
    return std::nullopt;
  }
  core::TrackItem item = toTrackItem(*record);
  const auto grid = LibraryRepository::getBeatgrid(readDb_, id);
  if (grid.has_value() && grid->bpm > 0.0 && record->sampleRate > 0) {
    item.bpm = grid->bpm;  // the grid (possibly edited by the user) is the authority on tempo
    item.firstBeatSec = static_cast<double>(grid->firstBeatFrame) / static_cast<double>(record->sampleRate);
  }
  return item;
}

void LibraryService::registerAnalysisHandler(std::string_view taskType, TaskHandler handler, int version) {
  analysis_.registerHandler(taskType, std::move(handler), version);
  analysisTypes_.emplace_back(taskType);
}

void LibraryService::startAnalysis(const std::filesystem::path& cacheDir) {
  // A newer analysis algorithm re-queues the tracks that were analysed with an older one (SPEC section 31).
  try {
    Database db = Database::open(dbPath_);
    for (const std::string& type : analysisTypes_) {
      (void)analysis_.requeueOutdatedTasks(db, type);
    }
    // Tasks that were cut short by the last shutdown run again.
    db.execute("UPDATE analysis_tasks SET status = 'pending' WHERE status = 'failed' AND error_message = 'cancelled';");
  } catch (const std::exception&) {
    // Not fatal: the tracks keep their previous results.
  }
  ensureAnalysisTasks();
  analysisWriter_ = std::make_unique<DatabaseWriter>(dbPath_);
  analysis_.startWorker(*analysisWriter_, dbPath_, cacheDir, 200);
}

void LibraryService::requestScan(const std::string& folderPath) {
  if (folderPath.empty()) {
    return;
  }
  rememberFolder(folderPath);
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (std::find(pending_.begin(), pending_.end(), folderPath) == pending_.end()) {
      pending_.push_back(folderPath);
    }
  }
  // Show "scanning" straight away: the worker may take a moment to pick the folder up.
  core::LibraryScanStatus status = scanStatus();
  status.scanning = true;
  setStatus(status);
  queueCv_.notify_one();
}

core::LibraryScanStatus LibraryService::scanStatus() const {
  core::LibraryScanStatus status;
  {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status = status_;
  }
  try {
    std::lock_guard<std::mutex> lock(readMutex_);
    status.analysisPending = analysis_.getPendingCount(const_cast<Database&>(readDb_));
  } catch (const std::exception&) {
    status.analysisPending = 0;  // a busy database must not break the status line
  }
  return status;
}

std::vector<std::string> LibraryService::knownFolders() const {
  std::lock_guard<std::mutex> lock(queueMutex_);
  return folders_;
}

void LibraryService::rescanKnownFolders() {
  for (const auto& folder : knownFolders()) {
    requestScan(folder);
  }
}

void LibraryService::rememberFolder(const std::string& folder) {
  std::lock_guard<std::mutex> lock(queueMutex_);
  if (std::find(folders_.begin(), folders_.end(), folder) != folders_.end()) {
    return;
  }
  folders_.push_back(folder);
  std::error_code ec;
  std::filesystem::create_directories(foldersFile_.parent_path(), ec);
  std::ofstream out(foldersFile_, std::ios::trunc);
  for (const auto& entry : folders_) {
    out << entry << "\n";
  }
}

void LibraryService::setStatus(const core::LibraryScanStatus& status) {
  std::lock_guard<std::mutex> lock(statusMutex_);
  status_ = status;
}

void LibraryService::workerLoop() {
  for (;;) {
    std::string folder;
    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      queueCv_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (stopping_) {
        return;
      }
      folder = std::move(pending_.front());
      pending_.pop_front();
    }

    std::string failure;
    try {
      Database db = openMigrated(dbPath_);
      const std::u8string utf8Folder(reinterpret_cast<const char8_t*>(folder.data()), folder.size());
      (void)scanner_.scanDirectorySync(std::filesystem::path(utf8Folder), db, ScanOptions{},
                                       [this](const ScanStatistics& stats) {
                                         core::LibraryScanStatus progress;
                                         progress.scanning = !stats.isComplete;
                                         progress.discovered = stats.totalDiscovered;
                                         progress.processed = stats.processed;
                                         progress.errors = stats.errors;
                                         progress.currentFile = stats.currentFile.filename().string();
                                         setStatus(progress);
                                       });
    } catch (const std::exception& error) {
      failure = error.what();
    }

    try {
      ensureAnalysisTasks();  // new tracks get every registered task type, not only the scanner's default list
    } catch (const std::exception& error) {
      if (failure.empty()) {
        failure = error.what();  // shown in the status; a database error must never end the program
      }
    }
    core::LibraryScanStatus finished = scanStatus();
    finished.currentFile.clear();
    finished.lastError = failure;
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      finished.scanning = !pending_.empty();
    }
    setStatus(finished);
  }
}

}  // namespace zyron::library
