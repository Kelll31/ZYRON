// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "Library/Analysis/AnalysisTaskQueue.hpp"
#include "Library/Database/Database.hpp"
#include "Library/Database/DatabaseWriter.hpp"
#include "Library/Database/LibraryRepository.hpp"
#include "Library/Database/Migrations.hpp"

using namespace zyron::library;

namespace {

struct TempDirectory {
  std::filesystem::path path;
  TempDirectory() {
    const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("zyron_queue_test_" + std::to_string(ts));
    std::filesystem::create_directories(path);
  }
  ~TempDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

void createSimpleWav(const std::filesystem::path& path, int sampleRate, int channels, int numSamples) {
  std::ofstream f(path, std::ios::binary);
  REQUIRE(f.is_open());

  const int bytesPerSample = 2;
  const uint32_t dataSize = numSamples * channels * bytesPerSample;
  const uint32_t riffSize = 36 + dataSize;

  f.write("RIFF", 4);
  f.write(reinterpret_cast<const char*>(&riffSize), 4);
  f.write("WAVE", 4);

  f.write("fmt ", 4);
  const uint32_t fmtSize = 16;
  const uint16_t audioFormat = 1;
  const uint16_t numCh = static_cast<uint16_t>(channels);
  const uint32_t sRate = static_cast<uint32_t>(sampleRate);
  const uint32_t byteRate = sRate * numCh * bytesPerSample;
  const uint16_t blockAlign = numCh * bytesPerSample;
  const uint16_t bitsPerSample = 16;

  f.write(reinterpret_cast<const char*>(&fmtSize), 4);
  f.write(reinterpret_cast<const char*>(&audioFormat), 2);
  f.write(reinterpret_cast<const char*>(&numCh), 2);
  f.write(reinterpret_cast<const char*>(&sRate), 4);
  f.write(reinterpret_cast<const char*>(&byteRate), 4);
  f.write(reinterpret_cast<const char*>(&blockAlign), 2);
  f.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

  f.write("data", 4);
  f.write(reinterpret_cast<const char*>(&dataSize), 4);
  std::vector<int16_t> silence(numSamples * channels, 0);
  f.write(reinterpret_cast<const char*>(silence.data()), dataSize);
}

}  // namespace

TEST_CASE("AnalysisTaskQueue pipeline and task execution", "[library][queue]") {
  TempDirectory temp;
  auto db = Database::openInMemory();
  Migrations::apply(db);

  const auto audioPath = temp.path / "track_sample.wav";
  createSimpleWav(audioPath, 48000, 2, 48000 * 2);  // 2 seconds @ 48 kHz

  TrackRecord track;
  track.filepath = audioPath.string();
  track.contentHash = "hash123456789";
  track.fileSize = 1000;
  track.title = "Sample Track";
  const auto trackId = LibraryRepository::insertTrack(db, track);
  REQUIRE(trackId > 0);

  AnalysisTaskQueue queue;

  SECTION("enqueues all standard tasks in pipeline (§31)") {
    queue.enqueueStandardPipeline(db, trackId);
    auto pending = LibraryRepository::getPendingAnalysisTasks(db, 100);
    REQUIRE(pending.size() == 6);
  }

  SECTION("processes duration task and updates track attributes") {
    queue.enqueueTask(db, trackId, "duration", 1);
    CHECK(queue.getPendingCount(db) == 1);

    bool processed = queue.processNextPendingTask(db, temp.path);
    CHECK(processed);
    CHECK(queue.getPendingCount(db) == 0);

    auto updatedTrack = LibraryRepository::findTrackById(db, trackId);
    REQUIRE(updatedTrack.has_value());
    CHECK(updatedTrack->sampleRate == 48000);
    CHECK(updatedTrack->channels == 2);
    CHECK_THAT(updatedTrack->durationSec, Catch::Matchers::WithinRel(2.0, 1e-2));
  }

  SECTION("processes waveform task and generates .zywv peaks cache file") {
    queue.enqueueTask(db, trackId, "waveform", 1);

    bool processed = queue.processNextPendingTask(db, temp.path);
    CHECK(processed);

    auto updatedTrack = LibraryRepository::findTrackById(db, trackId);
    REQUIRE(updatedTrack.has_value());
    CHECK_FALSE(updatedTrack->waveformPeaksPath.empty());

    const std::filesystem::path peaksFile(updatedTrack->waveformPeaksPath);
    CHECK(std::filesystem::exists(peaksFile));
    CHECK(peaksFile.extension() == ".zywv");
  }

  SECTION("task versioning re-queues only outdated tasks") {
    queue.enqueueTask(db, trackId, "waveform", 1);
    queue.processNextPendingTask(db, temp.path);
    CHECK(queue.getPendingCount(db) == 0);

    // Register a new v2 algorithm for waveform
    queue.registerHandler("waveform", [](const TaskContext&, std::string&) { return true; }, 2);

    // Requeue outdated tasks
    const auto requeued = queue.requeueOutdatedTasks(db, "waveform");
    CHECK(requeued == 1);
    CHECK(queue.getPendingCount(db) == 1);

    auto pending = LibraryRepository::getPendingAnalysisTasks(db, 10);
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].version == 2);
  }

  SECTION("error isolation records failure without disrupting pipeline") {
    TrackRecord missingTrack;
    missingTrack.filepath = "non_existent_audio_file.wav";
    missingTrack.contentHash = "missing_hash";
    const auto missingId = LibraryRepository::insertTrack(db, missingTrack);

    queue.enqueueTask(db, missingId, "waveform", 1);
    queue.enqueueTask(db, trackId, "duration", 1);

    // First task should fail
    CHECK(queue.processNextPendingTask(db, temp.path));

    // Second task should succeed
    CHECK(queue.processNextPendingTask(db, temp.path));

    auto tasks = LibraryRepository::getPendingAnalysisTasks(db, 10);
    CHECK(tasks.empty());  // none pending

    // Verify error message was recorded on failed task
    auto stmt = db.prepare("SELECT status, error_message FROM analysis_tasks WHERE track_id = ?;");
    stmt.bindInt64(1, missingId);
    REQUIRE(stmt.step());
    CHECK(stmt.getText(0) == "failed");
    CHECK_FALSE(stmt.getText(1).empty());
  }
}

TEST_CASE("AnalysisTaskQueue background worker", "[library][queue]") {
  TempDirectory temp;
  const auto dbFile = temp.path / "queue_worker.db";
  {
    Database initDb = Database::open(dbFile);
    Migrations::apply(initDb);
  }

  const auto audioPath = temp.path / "worker_test.wav";
  createSimpleWav(audioPath, 44100, 2, 44100);

  {
    Database setupDb = Database::open(dbFile);
    TrackRecord t;
    t.filepath = audioPath.string();
    t.contentHash = "worker_hash_1";
    const auto id = LibraryRepository::insertTrack(setupDb, t);
    LibraryRepository::enqueueAnalysisTask(setupDb, id, "duration", 1);
  }

  DatabaseWriter writer(dbFile);
  AnalysisTaskQueue queue;

  queue.startWorker(writer, dbFile, temp.path, 10);
  CHECK(queue.isWorkerRunning());

  // Wait for worker to finish processing
  const auto start = std::chrono::steady_clock::now();
  while (true) {
    Database checkDb = Database::open(dbFile);
    if (queue.getPendingCount(checkDb) == 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      FAIL("Analysis worker timed out");
    }
  }

  queue.stopWorker();
  CHECK_FALSE(queue.isWorkerRunning());

  // Verify track was updated
  Database verifyDb = Database::open(dbFile);
  auto allTracks = LibraryRepository::listAllTracks(verifyDb);
  REQUIRE(allTracks.size() == 1);
  CHECK(allTracks[0].sampleRate == 44100);
}
