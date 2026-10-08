// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <future>
#include <vector>

#include "Library/Database/Database.hpp"
#include "Library/Database/DatabaseWriter.hpp"
#include "Library/Database/LibraryRepository.hpp"
#include "Library/Database/Migrations.hpp"

using namespace zyron::library;

TEST_CASE("Database basic operations and transactions", "[library][database]") {
  auto db = Database::openInMemory();
  REQUIRE(db.isOpen());

  SECTION("can create tables, insert, and select values") {
    db.execute("CREATE TABLE test (id INTEGER PRIMARY KEY, val TEXT, num REAL);");

    auto insert = db.prepare("INSERT INTO test (val, num) VALUES (?, ?);");
    insert.bindText(1, "hello");
    insert.bindDouble(2, 3.14159);
    insert.execute();

    CHECK(db.lastInsertRowId() == 1);
    CHECK(db.changes() == 1);

    auto select = db.prepare("SELECT id, val, num FROM test WHERE id = 1;");
    REQUIRE(select.step());
    CHECK(select.getInt64(0) == 1);
    CHECK(select.getText(1) == "hello");
    CHECK_THAT(select.getDouble(2), Catch::Matchers::WithinRel(3.14159, 1e-4));
    CHECK_FALSE(select.step());
  }

  SECTION("transaction commit persists while rollback cancels") {
    db.execute("CREATE TABLE tx_test (id INTEGER PRIMARY KEY, msg TEXT);");

    db.beginTransaction();
    auto s1 = db.prepare("INSERT INTO tx_test (msg) VALUES ('committed');");
    s1.execute();
    db.commit();

    db.beginTransaction();
    auto s2 = db.prepare("INSERT INTO tx_test (msg) VALUES ('cancelled');");
    s2.execute();
    db.rollback();

    auto count = db.prepare("SELECT COUNT(*) FROM tx_test;");
    REQUIRE(count.step());
    CHECK(count.getInt(0) == 1);

    auto item = db.prepare("SELECT msg FROM tx_test;");
    REQUIRE(item.step());
    CHECK(item.getText(0) == "committed");
  }
}

TEST_CASE("Database file opens in WAL mode", "[library][database]") {
  const auto tempDir = std::filesystem::temp_directory_path();
  const auto dbFile = tempDir / "zyron_wal_test.db";
  std::filesystem::remove(dbFile);

  {
    auto db = Database::open(dbFile);
    auto stmt = db.prepare("PRAGMA journal_mode;");
    REQUIRE(stmt.step());
    CHECK(stmt.getText(0) == "wal");
  }

  std::filesystem::remove(dbFile);
  std::filesystem::remove(tempDir / "zyron_wal_test.db-wal");
  std::filesystem::remove(tempDir / "zyron_wal_test.db-shm");
}

TEST_CASE("Migrations apply schema v1 idempotently", "[library][migrations]") {
  auto db = Database::openInMemory();
  CHECK(db.userVersion() == 0);

  Migrations::apply(db);
  CHECK(db.userVersion() == Migrations::kCurrentSchemaVersion);

  // Applying again does not throw and leaves version intact
  REQUIRE_NOTHROW(Migrations::apply(db));
  CHECK(db.userVersion() == Migrations::kCurrentSchemaVersion);

  // Verify tables exist
  auto checkTable = [&db](std::string_view name) {
    auto stmt = db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?;");
    stmt.bindText(1, name);
    return stmt.step();
  };

  CHECK(checkTable("tracks"));
  CHECK(checkTable("playlists"));
  CHECK(checkTable("playlist_tracks"));
  CHECK(checkTable("cue_points"));
  CHECK(checkTable("beatgrids"));
  CHECK(checkTable("analysis_tasks"));
  CHECK(checkTable("tracks_fts"));
}

TEST_CASE("LibraryRepository tracks CRUD and identity by hash", "[library][repository]") {
  auto db = Database::openInMemory();
  Migrations::apply(db);

  TrackRecord t1;
  t1.filepath = "D:/Music/DnB/Sub Focus - Solar System.mp3";
  t1.contentHash = "hash_subfocus_solarsystem_12345";
  t1.fileSize = 10485760;
  t1.fileMtime = 1710000000;
  t1.title = "Solar System";
  t1.artist = "Sub Focus";
  t1.album = "Solar System / Siren";
  t1.genre = "Drum & Bass";
  t1.year = 2019;
  t1.bpm = 174.0;
  t1.key = "4A";
  t1.energy = 8.5;
  t1.durationSec = 288.5;
  t1.sampleRate = 44100;
  t1.channels = 2;

  const auto id1 = LibraryRepository::insertTrack(db, t1);
  REQUIRE(id1 > 0);

  SECTION("can find track by ID, path, and content hash") {
    auto byId = LibraryRepository::findTrackById(db, id1);
    REQUIRE(byId.has_value());
    CHECK(byId->title == "Solar System");
    CHECK(byId->artist == "Sub Focus");
    CHECK_THAT(byId->bpm, Catch::Matchers::WithinRel(174.0, 1e-4));
    CHECK(byId->key == "4A");

    auto byHash = LibraryRepository::findTrackByContentHash(db, "hash_subfocus_solarsystem_12345");
    REQUIRE(byHash.has_value());
    CHECK(byHash->id == id1);

    auto byPath = LibraryRepository::findTrackByPath(db, "D:/Music/DnB/Sub Focus - Solar System.mp3");
    REQUIRE(byPath.has_value());
    CHECK(byPath->id == id1);
  }

  SECTION("tracks can be updated and deleted") {
    t1.id = id1;
    t1.energy = 9.2;
    t1.stemStatus = "ready";
    LibraryRepository::updateTrack(db, t1);

    auto updated = LibraryRepository::findTrackById(db, id1);
    REQUIRE(updated.has_value());
    CHECK_THAT(updated->energy, Catch::Matchers::WithinRel(9.2, 1e-4));
    CHECK(updated->stemStatus == "ready");

    LibraryRepository::deleteTrack(db, id1);
    CHECK_FALSE(LibraryRepository::findTrackById(db, id1).has_value());
  }
}

TEST_CASE("LibraryRepository FTS5 full-text search", "[library][fts5]") {
  auto db = Database::openInMemory();
  Migrations::apply(db);

  TrackRecord t1;
  t1.filepath = "D:/Music/Chase & Status - Baddadan.wav";
  t1.contentHash = "hash_baddadan";
  t1.title = "Baddadan";
  t1.artist = "Chase & Status, Bou";
  t1.album = "2 RUFF, Vol. 1";
  t1.genre = "Drum & Bass";
  t1.bpm = 174.0;
  LibraryRepository::insertTrack(db, t1);

  TrackRecord t2;
  t2.filepath = "D:/Music/Hedex - MHITR.mp3";
  t2.contentHash = "hash_mhitr";
  t2.title = "My High Is The Rhythm";
  t2.artist = "Hedex";
  t2.album = "Single";
  t2.genre = "Jump Up";
  t2.bpm = 175.0;
  LibraryRepository::insertTrack(db, t2);

  TrackRecord t3;
  t3.filepath = "D:/Music/Calibre - Even If.flac";
  t3.contentHash = "hash_evenif";
  t3.title = "Even If";
  t3.artist = "Calibre";
  t3.album = "Even If EP";
  t3.genre = "Liquid DnB";
  t3.bpm = 172.0;
  LibraryRepository::insertTrack(db, t3);

  SECTION("search by artist") {
    auto res = LibraryRepository::searchTracks(db, "Chase");
    REQUIRE(res.size() == 1);
    CHECK(res[0].title == "Baddadan");
  }

  SECTION("search by title") {
    auto res = LibraryRepository::searchTracks(db, "Rhythm");
    REQUIRE(res.size() == 1);
    CHECK(res[0].artist == "Hedex");
  }

  SECTION("search by genre prefix") {
    auto res = LibraryRepository::searchTracks(db, "Liquid");
    REQUIRE(res.size() == 1);
    CHECK(res[0].title == "Even If");
  }
}

TEST_CASE("LibraryRepository cue points and beatgrid persistence", "[library][cues_grid]") {
  auto db = Database::openInMemory();
  Migrations::apply(db);

  TrackRecord t;
  t.filepath = "test.wav";
  t.contentHash = "hash_test";
  const auto trackId = LibraryRepository::insertTrack(db, t);

  SECTION("cue points can be saved and updated") {
    CuePointRecord cue0;
    cue0.trackId = trackId;
    cue0.index = 0;
    cue0.frame = 48000;
    cue0.name = "Drop 1";
    cue0.color = "#FF0000";
    cue0.type = "drop";
    cue0.source = "user";

    LibraryRepository::saveCuePoint(db, cue0);

    auto cues = LibraryRepository::getCuePoints(db, trackId);
    REQUIRE(cues.size() == 1);
    CHECK(cues[0].name == "Drop 1");
    CHECK(cues[0].frame == 48000);
    CHECK(cues[0].source == "user");

    // Updating same index overwrites
    cue0.frame = 96000;
    cue0.name = "Updated Drop";
    LibraryRepository::saveCuePoint(db, cue0);

    cues = LibraryRepository::getCuePoints(db, trackId);
    REQUIRE(cues.size() == 1);
    CHECK(cues[0].name == "Updated Drop");
    CHECK(cues[0].frame == 96000);
  }

  SECTION("beatgrid can be saved and retrieved with source=user protection (§16)") {
    BeatgridRecord grid;
    grid.trackId = trackId;
    grid.bpm = 174.0;
    grid.firstBeatFrame = 1200;
    grid.gridDataJson = "{\"bpm\":174.0}";
    grid.source = "auto";

    LibraryRepository::saveBeatgrid(db, grid);

    auto found = LibraryRepository::getBeatgrid(db, trackId);
    REQUIRE(found.has_value());
    CHECK_THAT(found->bpm, Catch::Matchers::WithinRel(174.0, 1e-4));
    CHECK(found->firstBeatFrame == 1200);
    CHECK(found->source == "auto");

    // 1. User edits the grid -> source becomes "user"
    BeatgridRecord userGrid;
    userGrid.trackId = trackId;
    userGrid.bpm = 174.5;
    userGrid.firstBeatFrame = 1400;
    userGrid.gridDataJson = "{\"bpm\":174.5,\"source\":\"user\"}";
    userGrid.source = "user";
    LibraryRepository::saveBeatgrid(db, userGrid);

    auto foundUser = LibraryRepository::getBeatgrid(db, trackId);
    REQUIRE(foundUser.has_value());
    CHECK_THAT(foundUser->bpm, Catch::Matchers::WithinRel(174.5, 1e-4));
    CHECK(foundUser->firstBeatFrame == 1400);
    CHECK(foundUser->source == "user");

    // 2. Automated background re-analysis tries to overwrite with source="auto" -> IGNORED
    BeatgridRecord autoOverwrite;
    autoOverwrite.trackId = trackId;
    autoOverwrite.bpm = 173.8;
    autoOverwrite.firstBeatFrame = 1100;
    autoOverwrite.gridDataJson = "{\"bpm\":173.8}";
    autoOverwrite.source = "auto";
    LibraryRepository::saveBeatgrid(db, autoOverwrite);

    auto protectedGrid = LibraryRepository::getBeatgrid(db, trackId);
    REQUIRE(protectedGrid.has_value());
    // Must remain the user's customized grid!
    CHECK_THAT(protectedGrid->bpm, Catch::Matchers::WithinRel(174.5, 1e-4));
    CHECK(protectedGrid->firstBeatFrame == 1400);
    CHECK(protectedGrid->source == "user");

    // 3. User edits again with source="user" -> ALLOWED
    userGrid.bpm = 175.0;
    LibraryRepository::saveBeatgrid(db, userGrid);
    auto updatedUser = LibraryRepository::getBeatgrid(db, trackId);
    REQUIRE(updatedUser.has_value());
    CHECK_THAT(updatedUser->bpm, Catch::Matchers::WithinRel(175.0, 1e-4));
  }
}

TEST_CASE("DatabaseWriter serializes concurrent writes on dedicated worker thread", "[library][writer]") {
  auto memDb = Database::openInMemory();
  DatabaseWriter writer(std::move(memDb));

  constexpr int kNumTasks = 20;
  std::vector<std::future<void>> futures;
  futures.reserve(kNumTasks);

  for (int i = 0; i < kNumTasks; ++i) {
    futures.push_back(writer.post([i](Database& db) {
      TrackRecord t;
      t.filepath = "track_" + std::to_string(i) + ".mp3";
      t.contentHash = "hash_" + std::to_string(i);
      t.title = "Track " + std::to_string(i);
      LibraryRepository::insertTrack(db, t);
    }));
  }

  // Wait for all futures
  for (auto& f : futures) {
    f.get();
  }

  // Verify all 20 tracks were written
  std::promise<int> countPromise;
  auto countFuture = countPromise.get_future();
  writer.post([&countPromise](Database& db) {
    const auto tracks = LibraryRepository::listAllTracks(db);
    countPromise.set_value(static_cast<int>(tracks.size()));
  });

  CHECK(countFuture.get() == kNumTasks);
  writer.stop();
}
