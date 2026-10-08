// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "Library/Database/Database.hpp"
#include "Library/Database/DatabaseWriter.hpp"
#include "Library/Database/LibraryRepository.hpp"
#include "Library/Database/Migrations.hpp"
#include "Library/Hash/ContentHasher.hpp"
#include "Library/Metadata/BuiltinMetadataReader.hpp"
#include "Library/Metadata/MetadataExtractor.hpp"
#include "Library/Scanner/LibraryScanner.hpp"

using namespace zyron::library;

namespace {

struct TempDirectory {
  std::filesystem::path path;
  TempDirectory() {
    const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("zyron_scan_test_" + std::to_string(ts));
    std::filesystem::create_directories(path);
  }
  ~TempDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

void createWavFile(const std::filesystem::path& path, int sampleRate, int channels, int numSamples,
                   const std::string& title = "", const std::string& artist = "", const std::string& album = "") {
  std::ofstream f(path, std::ios::binary);
  REQUIRE(f.is_open());

  const int bytesPerSample = 2;  // 16-bit PCM
  const uint32_t dataSize = numSamples * channels * bytesPerSample;

  // Build LIST INFO chunk if tags provided
  std::vector<uint8_t> infoChunk;
  if (!title.empty() || !artist.empty() || !album.empty()) {
    std::vector<uint8_t> listPayload;
    listPayload.push_back('I'); listPayload.push_back('N'); listPayload.push_back('F'); listPayload.push_back('O');

    auto appendSub = [&](const char* id, const std::string& str) {
      if (str.empty()) return;
      for (int i = 0; i < 4; ++i) listPayload.push_back(static_cast<uint8_t>(id[i]));
      uint32_t sz = static_cast<uint32_t>(str.size() + 1);  // include null
      listPayload.push_back(static_cast<uint8_t>(sz & 0xFF));
      listPayload.push_back(static_cast<uint8_t>((sz >> 8) & 0xFF));
      listPayload.push_back(static_cast<uint8_t>((sz >> 16) & 0xFF));
      listPayload.push_back(static_cast<uint8_t>((sz >> 24) & 0xFF));
      for (char c : str) listPayload.push_back(static_cast<uint8_t>(c));
      listPayload.push_back(0);
      if (sz & 1) listPayload.push_back(0);  // padding to word boundary
    };

    appendSub("INAM", title);
    appendSub("IART", artist);
    appendSub("IPRD", album);

    infoChunk.push_back('L'); infoChunk.push_back('I'); infoChunk.push_back('S'); infoChunk.push_back('T');
    uint32_t listSize = static_cast<uint32_t>(listPayload.size());
    infoChunk.push_back(static_cast<uint8_t>(listSize & 0xFF));
    infoChunk.push_back(static_cast<uint8_t>((listSize >> 8) & 0xFF));
    infoChunk.push_back(static_cast<uint8_t>((listSize >> 16) & 0xFF));
    infoChunk.push_back(static_cast<uint8_t>((listSize >> 24) & 0xFF));
    infoChunk.insert(infoChunk.end(), listPayload.begin(), listPayload.end());
  }

  const uint32_t riffSize = 36 + dataSize + static_cast<uint32_t>(infoChunk.size());

  // RIFF header
  f.write("RIFF", 4);
  f.write(reinterpret_cast<const char*>(&riffSize), 4);
  f.write("WAVE", 4);

  // fmt chunk
  f.write("fmt ", 4);
  const uint32_t fmtSize = 16;
  const uint16_t audioFormat = 1;  // PCM
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

  // info chunk if present
  if (!infoChunk.empty()) {
    f.write(reinterpret_cast<const char*>(infoChunk.data()), infoChunk.size());
  }

  // data chunk
  f.write("data", 4);
  f.write(reinterpret_cast<const char*>(&dataSize), 4);
  std::vector<int16_t> silence(numSamples * channels, 0);
  f.write(reinterpret_cast<const char*>(silence.data()), dataSize);
}

}  // namespace

TEST_CASE("ContentHasher SHA-256 standard compliance", "[library][hash]") {
  SECTION("empty string matches NIST standard vector") {
    const std::string emptyHash = ContentHasher::hashString("");
    CHECK(emptyHash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  }

  SECTION("abc matches NIST standard vector") {
    const std::string abcHash = ContentHasher::hashString("abc");
    CHECK(abcHash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  }

  SECTION("file hashing handles arbitrary streaming data") {
    TempDirectory temp;
    const auto filePath = temp.path / "sample.bin";
    {
      std::ofstream f(filePath, std::ios::binary);
      f << "abc";
    }
    std::string err;
    const std::string hash = ContentHasher::hashFile(filePath, &err);
    CHECK(hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(err.empty());
  }

  SECTION("hashFile reports error on non-existent file") {
    std::string err;
    const std::string hash = ContentHasher::hashFile("non_existent_file.xyz", &err);
    CHECK(hash.empty());
    CHECK_FALSE(err.empty());
  }
}

TEST_CASE("MetadataExtractor and BuiltinMetadataReader", "[library][metadata]") {
  TempDirectory temp;

  SECTION("extracts WAV stream info and LIST INFO metadata") {
    const auto wavPath = temp.path / "track1.wav";
    createWavFile(wavPath, 48000, 2, 48000, "Stigma", "Noisia", "Split the Atom");

    MetadataExtractor extractor;
    TrackMetadata meta;
    REQUIRE(extractor.extract(wavPath, meta));

    CHECK(meta.sampleRate == 48000);
    CHECK(meta.channels == 2);
    CHECK_THAT(meta.durationSec, Catch::Matchers::WithinRel(1.0, 1e-3));
    CHECK(meta.title == "Stigma");
    CHECK(meta.artist == "Noisia");
    CHECK(meta.album == "Split the Atom");
  }

  SECTION("falls back intelligently to filename when tags are absent") {
    const auto wavPath = temp.path / "Chase & Status - Program.wav";
    createWavFile(wavPath, 44100, 2, 88200);

    MetadataExtractor extractor;
    TrackMetadata meta;
    REQUIRE(extractor.extract(wavPath, meta));

    CHECK(meta.sampleRate == 44100);
    CHECK(meta.channels == 2);
    CHECK_THAT(meta.durationSec, Catch::Matchers::WithinRel(2.0, 1e-3));
    CHECK(meta.artist == "Chase & Status");
    CHECK(meta.title == "Program");
  }
}

TEST_CASE("LibraryScanner incremental discovery and DB indexing", "[library][scanner]") {
  TempDirectory temp;
  auto db = Database::openInMemory();
  Migrations::apply(db);

  // Create 3 test files in the directory
  const auto track1 = temp.path / "Noisia - Asteroids.wav";
  const auto track2 = temp.path / "Camo & Krooked - Aurora.wav";
  const auto track3 = temp.path / "Sub Focus - Solar System.wav";

  createWavFile(track1, 44100, 2, 44100);
  createWavFile(track2, 44100, 2, 44100 * 2);
  createWavFile(track3, 48000, 2, 48000);

  LibraryScanner scanner;

  SECTION("initial scan indexes all discovered files") {
    auto stats = scanner.scanDirectorySync(temp.path, db);
    CHECK(stats.totalDiscovered == 3);
    CHECK(stats.newlyAdded == 3);
    CHECK(stats.skipped == 0);
    CHECK(stats.updated == 0);
    CHECK(stats.errors == 0);
    CHECK(stats.isComplete);

    auto allTracks = LibraryRepository::listAllTracks(db);
    REQUIRE(allTracks.size() == 3);

    // Verify FTS search works on newly scanned library
    auto searchResults = LibraryRepository::searchTracks(db, "Solar System");
    REQUIRE(searchResults.size() == 1);
    CHECK(searchResults[0].artist == "Sub Focus");

    // Verify analysis tasks were enqueued for each track
    auto pendingTasks = LibraryRepository::getPendingAnalysisTasks(db, 100);
    CHECK(pendingTasks.size() == 18);  // 6 tasks per track * 3 tracks
  }

  SECTION("subsequent scan skips unchanged files (incremental)") {
    scanner.scanDirectorySync(temp.path, db);

    // Rescan immediately without modifying files
    auto stats2 = scanner.scanDirectorySync(temp.path, db);
    CHECK(stats2.totalDiscovered == 3);
    CHECK(stats2.newlyAdded == 0);
    CHECK(stats2.skipped == 3);
    CHECK(stats2.updated == 0);
    CHECK(stats2.errors == 0);
  }

  SECTION("moved or renamed file preserves content hash identity and cues") {
    scanner.scanDirectorySync(temp.path, db);

    auto trackOpt = LibraryRepository::findTrackByPath(db, track1);
    REQUIRE(trackOpt.has_value());

    // User adds a hot cue on this track
    CuePointRecord cue;
    cue.trackId = trackOpt->id;
    cue.index = 1;
    cue.frame = 1000;
    cue.name = "Drop 1";
    cue.color = "#FF0000";
    cue.source = "user";
    LibraryRepository::saveCuePoint(db, cue);

    // Rename file on disk: track1 -> renamed.wav
    const auto renamed = temp.path / "Renamed - Asteroids.wav";
    std::filesystem::rename(track1, renamed);

    // Run scanner again
    auto stats = scanner.scanDirectorySync(temp.path, db);
    CHECK(stats.updated == 1);  // detected moved file by content hash!
    CHECK(stats.skipped == 2);
    CHECK(stats.newlyAdded == 0);

    // Verify cue point survived and track points to new path
    auto updatedTrack = LibraryRepository::findTrackByContentHash(db, trackOpt->contentHash);
    REQUIRE(updatedTrack.has_value());
    CHECK(updatedTrack->id == trackOpt->id);

    auto cues = LibraryRepository::getCuePoints(db, updatedTrack->id);
    REQUIRE(cues.size() == 1);
    CHECK(cues[0].name == "Drop 1");
    CHECK(cues[0].source == "user");
  }
}

TEST_CASE("LibraryScanner background scan with DatabaseWriter", "[library][scanner]") {
  TempDirectory temp;
  const auto dbFile = temp.path / "zyron_test.db";
  {
    Database initDb = Database::open(dbFile);
    Migrations::apply(initDb);
  }

  const auto audioDir = temp.path / "music";
  std::filesystem::create_directories(audioDir);
  createWavFile(audioDir / "Track A.wav", 44100, 2, 44100, "Alpha", "Artist 1");
  createWavFile(audioDir / "Track B.wav", 44100, 2, 88200, "Beta", "Artist 2");

  DatabaseWriter writer(dbFile);
  LibraryScanner scanner;

  std::atomic<int> callbackCount{0};
  scanner.startScanAsync(audioDir, writer, dbFile, {}, [&](const ScanStatistics& stats) {
    callbackCount++;
    if (stats.isComplete) {
      // Completed callback
    }
  });

  // Wait for scanner to finish
  const auto start = std::chrono::steady_clock::now();
  while (scanner.isScanning()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
      FAIL("Scanner timed out in background test");
    }
  }

  scanner.stopScan();
  writer.flush();

  auto finalStats = scanner.currentStatistics();
  CHECK(finalStats.totalDiscovered == 2);
  CHECK(finalStats.newlyAdded == 2);
  CHECK(finalStats.isComplete);
  CHECK(callbackCount > 0);

  // Check database contents
  Database verifyDb = Database::open(dbFile);
  auto tracks = LibraryRepository::listAllTracks(verifyDb);
  CHECK(tracks.size() == 2);
}
