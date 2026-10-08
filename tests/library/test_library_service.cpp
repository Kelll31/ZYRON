// SPDX-License-Identifier: AGPL-3.0-only
// LibraryService: markers, analysis priority/reanalysis, task counts, cancelled-task requeue; schema migration v2.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "Library/Database/Database.hpp"
#include "Library/Database/LibraryRepository.hpp"
#include "Library/Database/Migrations.hpp"
#include "Library/LibraryService.hpp"

using namespace zyron::library;
using zyron::core::TrackMarker;

namespace {

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    static std::atomic<int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("zyron_libservice_" + std::to_string(stamp) + "_" + std::to_string(counter++));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  [[nodiscard]] std::filesystem::path db() const { return path / "library.db"; }
  [[nodiscard]] std::filesystem::path folders() const { return path / "folders.txt"; }
};

/// Creates the database (migrated) and returns an open connection for arranging fixtures.
Database openFixtureDb(const TempDir& dir) {
  Database db = Database::open(dir.db());
  Migrations::apply(db);
  return db;
}

std::int64_t addTrack(Database& db, const std::string& name, int sampleRate = 44100) {
  TrackRecord track;
  track.filepath = "C:/music/" + name + ".wav";
  track.contentHash = "hash_" + name;
  track.title = name;
  track.sampleRate = sampleRate;
  return LibraryRepository::insertTrack(db, track);
}

void addTask(Database& db, std::int64_t trackId, const std::string& type, const std::string& status,
             const std::string& error = "") {
  LibraryRepository::enqueueAnalysisTask(db, trackId, type, 1);
  LibraryRepository::updateAnalysisTaskStatus(db, trackId, type, status, error);
}

struct TaskRow {
  std::string status;
  int priority{0};
};

TaskRow taskRow(Database& db, std::int64_t trackId, const std::string& type) {
  auto stmt = db.prepare("SELECT status, priority FROM analysis_tasks WHERE track_id = ? AND task_type = ?;");
  stmt.bindInt64(1, trackId);
  stmt.bindText(2, type);
  REQUIRE(stmt.step());
  return TaskRow{stmt.getText(0), stmt.getInt(1)};
}

TrackMarker marker(const char* type, double timeSec, const char* name = "") {
  TrackMarker m;
  m.type = type;
  m.timeSec = timeSec;
  m.name = name;
  return m;
}

}  // namespace

TEST_CASE("setMarker stores user markers and markers() returns them ordered by time", "[library][service][markers]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
  }
  LibraryService service(dir.db(), dir.folders());

  const int late = service.setMarker(track, marker(TrackMarker::kDrop, 30.0, "drop"));
  const int early = service.setMarker(track, marker(TrackMarker::kMixIn, 10.0));
  const int middle = service.setMarker(track, marker(TrackMarker::kBreak, 20.0));

  CHECK(late >= LibraryService::kFirstMarkerIndex);
  CHECK(early > late);
  CHECK(middle > early);

  const auto list = service.markers(track);
  REQUIRE(list.size() == 3);
  CHECK(list[0].timeSec == 10.0);
  CHECK(list[0].type == TrackMarker::kMixIn);
  CHECK(list[1].timeSec == 20.0);
  CHECK(list[2].timeSec == 30.0);
  CHECK(list[2].name == "drop");
  CHECK(list[2].source == "user");
  CHECK(list[2].id == late);
}

TEST_CASE("setMarker with an existing id moves that marker", "[library][service][markers]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
  }
  LibraryService service(dir.db(), dir.folders());
  const int id = service.setMarker(track, marker(TrackMarker::kDrop, 30.0));

  TrackMarker moved = marker(TrackMarker::kDrop, 45.5);
  moved.id = id;
  CHECK(service.setMarker(track, moved) == id);

  const auto list = service.markers(track);
  REQUIRE(list.size() == 1);
  CHECK(list[0].timeSec == 45.5);
}

TEST_CASE("setMarker rejects unknown tracks and negative times", "[library][service][markers]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
  }
  LibraryService service(dir.db(), dir.folders());

  CHECK(service.setMarker(track + 100, marker(TrackMarker::kDrop, 1.0)) == 0);
  CHECK(service.setMarker(track, marker(TrackMarker::kDrop, -1.0)) == 0);
  CHECK(service.markers(track).empty());
  CHECK(service.markers(track + 100).empty());
}

TEST_CASE("removeMarker deletes a marker but never a hot cue", "[library][service][markers]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
    CuePointRecord hot;
    hot.trackId = track;
    hot.index = 1;
    hot.frame = 1000;
    hot.source = "user";
    LibraryRepository::saveCuePoint(db, hot);
  }
  LibraryService service(dir.db(), dir.folders());
  const int id = service.setMarker(track, marker(TrackMarker::kDrop, 5.0));
  REQUIRE(service.markers(track).size() == 1);

  service.removeMarker(track, 1);  // a hot cue index: ignored
  Database check = Database::open(dir.db());
  CHECK(LibraryRepository::getCuePoints(check, track).size() == 2);

  service.removeMarker(track, id);
  CHECK(service.markers(track).empty());
  CHECK(LibraryRepository::getCuePoints(check, track).size() == 1);  // the hot cue stays
}

TEST_CASE("user markers survive removeAutoCues while AI markers are replaced", "[library][service][markers]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
  }
  LibraryService service(dir.db(), dir.folders());
  const int userId = service.setMarker(track, marker(TrackMarker::kMixOut, 60.0));

  Database db = Database::open(dir.db());
  CuePointRecord autoCue;
  autoCue.trackId = track;
  autoCue.index = userId + 1;
  autoCue.frame = 44100 * 10;
  autoCue.type = TrackMarker::kDrop;
  autoCue.source = "auto";
  LibraryRepository::saveCuePoint(db, autoCue);
  REQUIRE(service.markers(track).size() == 2);

  LibraryRepository::removeAutoCues(db, track, LibraryService::kFirstMarkerIndex);

  const auto list = service.markers(track);
  REQUIRE(list.size() == 1);
  CHECK(list[0].id == userId);
  CHECK(list[0].source == "user");
}

TEST_CASE("an automatic cue cannot overwrite a user marker at the same index", "[library][service][markers]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
  }
  LibraryService service(dir.db(), dir.folders());
  const int userId = service.setMarker(track, marker(TrackMarker::kMixOut, 60.0));

  Database db = Database::open(dir.db());
  CuePointRecord autoCue;
  autoCue.trackId = track;
  autoCue.index = userId;
  autoCue.frame = 1;
  autoCue.source = "auto";
  LibraryRepository::saveCuePoint(db, autoCue);

  const auto list = service.markers(track);
  REQUIRE(list.size() == 1);
  CHECK(list[0].timeSec == 60.0);
}

TEST_CASE("markers convert frames using the track sample rate", "[library][service][markers]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "hi", 96000);
  }
  LibraryService service(dir.db(), dir.folders());
  service.setMarker(track, marker(TrackMarker::kDrop, 2.5));

  const auto list = service.markers(track);
  REQUIRE(list.size() == 1);
  CHECK(list[0].timeSec == 2.5);
}

TEST_CASE("prioritizeAnalysis raises pending and failed tasks only", "[library][service][analysis]") {
  TempDir dir;
  std::int64_t track = 0;
  std::int64_t other = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
    other = addTrack(db, "b");
    addTask(db, track, "bpm", "pending");
    addTask(db, track, "key", "failed", "boom");
    addTask(db, track, "energy", "completed");
    addTask(db, track, "waveform", "running");
    addTask(db, other, "bpm", "pending");
  }
  LibraryService service(dir.db(), dir.folders());

  service.prioritizeAnalysis(track);

  Database db = Database::open(dir.db());
  CHECK(taskRow(db, track, "bpm").priority == 1);
  const TaskRow failed = taskRow(db, track, "key");
  CHECK(failed.priority == 1);
  CHECK(failed.status == "pending");  // a failed task is retried
  CHECK(taskRow(db, track, "energy").priority == 0);
  CHECK(taskRow(db, track, "energy").status == "completed");
  CHECK(taskRow(db, track, "waveform").status == "running");
  CHECK(taskRow(db, other, "bpm").priority == 0);  // other tracks keep their place
}

TEST_CASE("reanalyze re-queues every task of the track at the front", "[library][service][analysis]") {
  TempDir dir;
  std::int64_t track = 0;
  std::int64_t other = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
    other = addTrack(db, "b");
    addTask(db, track, "bpm", "completed");
    addTask(db, track, "key", "failed", "boom");
    addTask(db, other, "bpm", "completed");
  }
  LibraryService service(dir.db(), dir.folders());

  service.reanalyze(track);

  Database db = Database::open(dir.db());
  CHECK(taskRow(db, track, "bpm").status == "pending");
  CHECK(taskRow(db, track, "bpm").priority == 1);
  CHECK(taskRow(db, track, "key").status == "pending");
  CHECK(taskRow(db, other, "bpm").status == "completed");
  CHECK(taskRow(db, other, "bpm").priority == 0);
}

TEST_CASE("listAll reports analysis task counts per track", "[library][service][analysis]") {
  TempDir dir;
  std::int64_t busy = 0;
  std::int64_t idle = 0;
  {
    Database db = openFixtureDb(dir);
    busy = addTrack(db, "busy");
    idle = addTrack(db, "idle");
    addTask(db, busy, "bpm", "completed");
    addTask(db, busy, "key", "failed", "boom");
    addTask(db, busy, "energy", "pending");
    addTask(db, busy, "waveform", "running");
  }
  LibraryService service(dir.db(), dir.folders());

  const auto items = service.listAll();
  REQUIRE(items.size() == 2);
  for (const auto& item : items) {
    if (item.id == busy) {
      CHECK(item.analysisTotal == 4);
      CHECK(item.analysisDone == 2);  // failed tasks count as finished
      CHECK(item.analysisFailed == 1);
    } else {
      REQUIRE(item.id == idle);
      CHECK(item.analysisTotal == 0);
      CHECK(item.analysisDone == 0);
      CHECK(item.analysisFailed == 0);
    }
  }
}

TEST_CASE("startAnalysis runs again the tasks cancelled by the last shutdown", "[library][service][analysis]") {
  TempDir dir;
  std::int64_t interrupted = 0;
  std::int64_t broken = 0;
  {
    Database db = openFixtureDb(dir);
    interrupted = addTrack(db, "interrupted");
    broken = addTrack(db, "broken");
    addTask(db, interrupted, "probe", "failed", "cancelled");
    addTask(db, broken, "probe", "failed", "boom");
  }

  std::mutex mutex;
  std::condition_variable cv;
  std::set<std::int64_t> ran;
  {
    LibraryService service(dir.db(), dir.folders());
    service.registerAnalysisHandler(
        "probe",
        [&](const TaskContext& ctx, std::string&) {
          std::lock_guard<std::mutex> lock(mutex);
          ran.insert(ctx.trackId);
          cv.notify_all();
          return true;
        },
        1);
    service.startAnalysis(dir.path / "cache");

    std::unique_lock<std::mutex> lock(mutex);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(10), [&] { return ran.count(interrupted) > 0; }));
  }  // destroying the service joins the worker

  CHECK(ran.count(broken) == 0);
  Database db = Database::open(dir.db());
  CHECK(taskRow(db, interrupted, "probe").status == "completed");
  const TaskRow stillBroken = taskRow(db, broken, "probe");
  CHECK(stillBroken.status == "failed");
}

TEST_CASE("migration v2 upgrades a version-1 database with a priority column", "[library][migrations]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
    addTask(db, track, "bpm", "pending");
    // Turn it back into a v1 database.
    db.execute("ALTER TABLE analysis_tasks DROP COLUMN priority;");
    db.setUserVersion(1);
    REQUIRE(db.userVersion() == 1);
  }

  Database db = Database::open(dir.db());
  REQUIRE(db.userVersion() == 1);
  Migrations::apply(db);

  CHECK(db.userVersion() == Migrations::kCurrentSchemaVersion);
  const TaskRow row = taskRow(db, track, "bpm");  // selecting `priority` proves the column exists
  CHECK(row.priority == 0);                       // existing rows get the default
  CHECK(row.status == "pending");
}

TEST_CASE("migrations are idempotent on an up-to-date database", "[library][migrations]") {
  TempDir dir;
  Database db = openFixtureDb(dir);
  REQUIRE(db.userVersion() == Migrations::kCurrentSchemaVersion);
  CHECK_NOTHROW(Migrations::apply(db));
  CHECK(db.userVersion() == Migrations::kCurrentSchemaVersion);
}

TEST_CASE("migration v3 adds loudness and bar profile to a version-2 database, keeping the rows", "[library][migrations]") {
  TempDir dir;
  std::int64_t track = 0;
  {
    Database db = openFixtureDb(dir);
    track = addTrack(db, "a");
    // Turn it back into a v2 database.
    db.execute("ALTER TABLE tracks DROP COLUMN loudness_lufs;");
    db.execute("ALTER TABLE tracks DROP COLUMN bar_profile;");
    db.setUserVersion(2);
  }

  Database db = Database::open(dir.db());
  REQUIRE(db.userVersion() == 2);
  Migrations::apply(db);
  CHECK(db.userVersion() == Migrations::kCurrentSchemaVersion);

  const auto found = LibraryRepository::findTrackById(db, track);
  REQUIRE(found.has_value());
  CHECK(found->title == "a");
  CHECK(found->loudnessLufs == 0.0);  // not measured yet
  CHECK(found->barProfile.empty());
}

TEST_CASE("migration v3 survives a half-applied earlier run (column already there)", "[library][migrations]") {
  TempDir dir;
  {
    Database db = openFixtureDb(dir);
    db.execute("ALTER TABLE tracks DROP COLUMN bar_profile;");  // only one of the two columns is left
    db.setUserVersion(2);
  }
  Database db = Database::open(dir.db());
  REQUIRE_NOTHROW(Migrations::apply(db));
  CHECK(db.userVersion() == Migrations::kCurrentSchemaVersion);
  CHECK(LibraryRepository::listAllTracks(db).empty());  // selects both columns: they exist now
}

TEST_CASE("loudness and bar profile round-trip through the repository and the track item", "[library]") {
  TempDir dir;
  Database db = openFixtureDb(dir);
  const std::int64_t id = addTrack(db, "b");
  auto record = LibraryRepository::findTrackById(db, id);
  REQUIRE(record.has_value());
  record->loudnessLufs = -9.5;
  record->barProfile = "ZB1;0.0000;1.411765;2;FF80;0000";
  LibraryRepository::updateTrack(db, *record);

  const auto back = LibraryRepository::findTrackById(db, id);
  REQUIRE(back.has_value());
  CHECK(back->loudnessLufs == -9.5);
  CHECK(back->barProfile == "ZB1;0.0000;1.411765;2;FF80;0000");
  CHECK(LibraryRepository::searchTracks(db, "b").front().loudnessLufs == -9.5);

  LibraryService service(dir.db(), dir.folders());
  const auto item = service.findTrack(id);
  REQUIRE(item.has_value());
  CHECK(item->loudnessLufs == -9.5);
  CHECK(service.barProfile(id) == "ZB1;0.0000;1.411765;2;FF80;0000");
  CHECK(service.barProfile(id + 1000).empty());
}
