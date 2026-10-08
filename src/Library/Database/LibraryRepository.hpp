// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Library/Database/Database.hpp"

namespace zyron::library {

struct TrackRecord {
  std::int64_t id{0};
  std::string filepath;
  std::string contentHash;
  std::int64_t fileSize{0};
  std::int64_t fileMtime{0};
  std::string title;
  std::string artist;
  std::string album;
  std::string genre;
  int year{0};
  double bpm{0.0};
  std::string key;
  double energy{0.0};
  double durationSec{0.0};
  int sampleRate{44100};
  int channels{2};
  std::string waveformPeaksPath;
  std::string stemStatus{"none"};
};

struct CuePointRecord {
  std::int64_t id{0};
  std::int64_t trackId{0};
  int index{0};
  std::int64_t frame{0};
  std::string name;
  std::string color;
  std::string type{"cue"};
  std::string source{"auto"};  // "auto" or "user"
};

struct BeatgridRecord {
  std::int64_t trackId{0};
  double bpm{0.0};
  std::int64_t firstBeatFrame{0};
  std::string gridDataJson;
  std::string source{"auto"};  // "auto" or "user"
};

struct AnalysisTaskRecord {
  std::int64_t id{0};
  std::int64_t trackId{0};
  std::string taskType;
  std::string status{"pending"};
  int version{1};
  std::string errorMessage;
};

/// High-level typed data access for the ZYRON library (SPEC section 28, 30).
class LibraryRepository {
 public:
  static std::int64_t insertTrack(Database& db, const TrackRecord& track);
  static void updateTrack(Database& db, const TrackRecord& track);
  static void deleteTrack(Database& db, std::int64_t id);

  [[nodiscard]] static std::optional<TrackRecord> findTrackById(Database& db, std::int64_t id);
  [[nodiscard]] static std::optional<TrackRecord> findTrackByContentHash(Database& db, std::string_view hash);
  [[nodiscard]] static std::optional<TrackRecord> findTrackByPath(Database& db, const std::filesystem::path& path);

  /// Full-text search across artist, title, album, genre, filepath via FTS5 (SPEC section 30).
  [[nodiscard]] static std::vector<TrackRecord> searchTracks(Database& db, std::string_view query);

  [[nodiscard]] static std::vector<TrackRecord> listAllTracks(Database& db);

  // Cue points
  static void saveCuePoint(Database& db, const CuePointRecord& cue);
  [[nodiscard]] static std::vector<CuePointRecord> getCuePoints(Database& db, std::int64_t trackId);

  // Beatgrid
  static void saveBeatgrid(Database& db, const BeatgridRecord& grid);
  [[nodiscard]] static std::optional<BeatgridRecord> getBeatgrid(Database& db, std::int64_t trackId);

  // Analysis tasks
  static void enqueueAnalysisTask(Database& db, std::int64_t trackId, std::string_view taskType, int version = 1);
  [[nodiscard]] static std::vector<AnalysisTaskRecord> getPendingAnalysisTasks(Database& db, int limit = 100);
  static void updateAnalysisTaskStatus(Database& db, std::int64_t trackId, std::string_view taskType,
                                       std::string_view status, std::string_view errorMessage = "");
};

}  // namespace zyron::library
