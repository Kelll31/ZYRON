// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Database/LibraryRepository.hpp"

namespace zyron::library {

namespace {

TrackRecord readTrackFromStatement(const Statement& stmt) {
  TrackRecord r;
  r.id = stmt.getInt64(0);
  r.filepath = stmt.getText(1);
  r.contentHash = stmt.getText(2);
  r.fileSize = stmt.getInt64(3);
  r.fileMtime = stmt.getInt64(4);
  r.title = stmt.getText(5);
  r.artist = stmt.getText(6);
  r.album = stmt.getText(7);
  r.genre = stmt.getText(8);
  r.year = stmt.getInt(9);
  r.bpm = stmt.getDouble(10);
  r.key = stmt.getText(11);
  r.energy = stmt.getDouble(12);
  r.durationSec = stmt.getDouble(13);
  r.sampleRate = stmt.getInt(14);
  r.channels = stmt.getInt(15);
  r.waveformPeaksPath = stmt.getText(16);
  r.stemStatus = stmt.getText(17);
  return r;
}

}  // namespace

std::int64_t LibraryRepository::insertTrack(Database& db, const TrackRecord& track) {
  auto stmt = db.prepare(R"(
    INSERT INTO tracks (
      filepath, content_hash, file_size, file_mtime,
      title, artist, album, genre, year,
      bpm, key, energy, duration_sec,
      sample_rate, channels, waveform_peaks_path, stem_status
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
  )");

  stmt.bindText(1, track.filepath);
  stmt.bindText(2, track.contentHash);
  stmt.bindInt64(3, track.fileSize);
  stmt.bindInt64(4, track.fileMtime);
  stmt.bindText(5, track.title);
  stmt.bindText(6, track.artist);
  stmt.bindText(7, track.album);
  stmt.bindText(8, track.genre);
  stmt.bindInt(9, track.year);
  stmt.bindDouble(10, track.bpm);
  stmt.bindText(11, track.key);
  stmt.bindDouble(12, track.energy);
  stmt.bindDouble(13, track.durationSec);
  stmt.bindInt(14, track.sampleRate);
  stmt.bindInt(15, track.channels);
  stmt.bindText(16, track.waveformPeaksPath);
  stmt.bindText(17, track.stemStatus);

  stmt.execute();
  return db.lastInsertRowId();
}

void LibraryRepository::updateTrack(Database& db, const TrackRecord& track) {
  auto stmt = db.prepare(R"(
    UPDATE tracks SET
      filepath = ?, content_hash = ?, file_size = ?, file_mtime = ?,
      title = ?, artist = ?, album = ?, genre = ?, year = ?,
      bpm = ?, key = ?, energy = ?, duration_sec = ?,
      sample_rate = ?, channels = ?, waveform_peaks_path = ?, stem_status = ?,
      updated_at = (strftime('%s', 'now'))
    WHERE id = ?;
  )");

  stmt.bindText(1, track.filepath);
  stmt.bindText(2, track.contentHash);
  stmt.bindInt64(3, track.fileSize);
  stmt.bindInt64(4, track.fileMtime);
  stmt.bindText(5, track.title);
  stmt.bindText(6, track.artist);
  stmt.bindText(7, track.album);
  stmt.bindText(8, track.genre);
  stmt.bindInt(9, track.year);
  stmt.bindDouble(10, track.bpm);
  stmt.bindText(11, track.key);
  stmt.bindDouble(12, track.energy);
  stmt.bindDouble(13, track.durationSec);
  stmt.bindInt(14, track.sampleRate);
  stmt.bindInt(15, track.channels);
  stmt.bindText(16, track.waveformPeaksPath);
  stmt.bindText(17, track.stemStatus);
  stmt.bindInt64(18, track.id);

  stmt.execute();
}

void LibraryRepository::deleteTrack(Database& db, std::int64_t id) {
  auto stmt = db.prepare("DELETE FROM tracks WHERE id = ?;");
  stmt.bindInt64(1, id);
  stmt.execute();
}

std::optional<TrackRecord> LibraryRepository::findTrackById(Database& db, std::int64_t id) {
  auto stmt = db.prepare(R"(
    SELECT
      id, filepath, content_hash, file_size, file_mtime,
      title, artist, album, genre, year,
      bpm, key, energy, duration_sec,
      sample_rate, channels, waveform_peaks_path, stem_status
    FROM tracks WHERE id = ?;
  )");
  stmt.bindInt64(1, id);

  if (stmt.step()) {
    return readTrackFromStatement(stmt);
  }
  return std::nullopt;
}

std::optional<TrackRecord> LibraryRepository::findTrackByContentHash(Database& db, std::string_view hash) {
  auto stmt = db.prepare(R"(
    SELECT
      id, filepath, content_hash, file_size, file_mtime,
      title, artist, album, genre, year,
      bpm, key, energy, duration_sec,
      sample_rate, channels, waveform_peaks_path, stem_status
    FROM tracks WHERE content_hash = ?;
  )");
  stmt.bindText(1, hash);

  if (stmt.step()) {
    return readTrackFromStatement(stmt);
  }
  return std::nullopt;
}

std::optional<TrackRecord> LibraryRepository::findTrackByPath(Database& db, const std::filesystem::path& path) {
  auto stmt = db.prepare(R"(
    SELECT
      id, filepath, content_hash, file_size, file_mtime,
      title, artist, album, genre, year,
      bpm, key, energy, duration_sec,
      sample_rate, channels, waveform_peaks_path, stem_status
    FROM tracks WHERE filepath = ?;
  )");
  const auto u8 = path.u8string();
  stmt.bindText(1, std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));

  if (stmt.step()) {
    return readTrackFromStatement(stmt);
  }
  return std::nullopt;
}

std::vector<TrackRecord> LibraryRepository::searchTracks(Database& db, std::string_view query) {
  if (query.empty()) {
    return listAllTracks(db);
  }

  // Sanitize and format for prefix FTS5 query
  std::string ftsQuery;
  for (char c : query) {
    if (c != '"' && c != '\'' && c != '*' && c != ':') {
      ftsQuery.push_back(c);
    }
  }
  if (ftsQuery.empty()) {
    return listAllTracks(db);
  }
  ftsQuery += "*";

  auto stmt = db.prepare(R"(
    SELECT
      t.id, t.filepath, t.content_hash, t.file_size, t.file_mtime,
      t.title, t.artist, t.album, t.genre, t.year,
      t.bpm, t.key, t.energy, t.duration_sec,
      t.sample_rate, t.channels, t.waveform_peaks_path, t.stem_status
    FROM tracks t
    JOIN tracks_fts f ON t.id = f.rowid
    WHERE tracks_fts MATCH ?
    ORDER BY rank;
  )");
  stmt.bindText(1, ftsQuery);

  std::vector<TrackRecord> results;
  while (stmt.step()) {
    results.push_back(readTrackFromStatement(stmt));
  }
  return results;
}

std::vector<TrackRecord> LibraryRepository::listAllTracks(Database& db) {
  auto stmt = db.prepare(R"(
    SELECT
      id, filepath, content_hash, file_size, file_mtime,
      title, artist, album, genre, year,
      bpm, key, energy, duration_sec,
      sample_rate, channels, waveform_peaks_path, stem_status
    FROM tracks
    ORDER BY artist, title;
  )");

  std::vector<TrackRecord> results;
  while (stmt.step()) {
    results.push_back(readTrackFromStatement(stmt));
  }
  return results;
}

void LibraryRepository::saveCuePoint(Database& db, const CuePointRecord& cue) {
  auto stmt = db.prepare(R"(
    INSERT INTO cue_points (track_id, cue_index, frame, name, color, type, source)
    VALUES (?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(track_id, cue_index) DO UPDATE SET
      frame = excluded.frame,
      name = excluded.name,
      color = excluded.color,
      type = excluded.type,
      source = excluded.source
    WHERE cue_points.source != 'user' OR excluded.source = 'user';
  )");

  stmt.bindInt64(1, cue.trackId);
  stmt.bindInt(2, cue.index);
  stmt.bindInt64(3, cue.frame);
  stmt.bindText(4, cue.name);
  stmt.bindText(5, cue.color);
  stmt.bindText(6, cue.type);
  stmt.bindText(7, cue.source);

  stmt.execute();
}

std::vector<CuePointRecord> LibraryRepository::getCuePoints(Database& db, std::int64_t trackId) {
  auto stmt = db.prepare(R"(
    SELECT id, track_id, cue_index, frame, name, color, type, source
    FROM cue_points
    WHERE track_id = ?
    ORDER BY cue_index;
  )");
  stmt.bindInt64(1, trackId);

  std::vector<CuePointRecord> cues;
  while (stmt.step()) {
    CuePointRecord c;
    c.id = stmt.getInt64(0);
    c.trackId = stmt.getInt64(1);
    c.index = stmt.getInt(2);
    c.frame = stmt.getInt64(3);
    c.name = stmt.getText(4);
    c.color = stmt.getText(5);
    c.type = stmt.getText(6);
    c.source = stmt.getText(7);
    cues.push_back(c);
  }
  return cues;
}

void LibraryRepository::saveBeatgrid(Database& db, const BeatgridRecord& grid) {
  auto stmt = db.prepare(R"(
    INSERT INTO beatgrids (track_id, bpm, first_beat_frame, grid_data_json, source)
    VALUES (?, ?, ?, ?, ?)
    ON CONFLICT(track_id) DO UPDATE SET
      bpm = excluded.bpm,
      first_beat_frame = excluded.first_beat_frame,
      grid_data_json = excluded.grid_data_json,
      source = excluded.source
    WHERE beatgrids.source != 'user' OR excluded.source = 'user';
  )");

  stmt.bindInt64(1, grid.trackId);
  stmt.bindDouble(2, grid.bpm);
  stmt.bindInt64(3, grid.firstBeatFrame);
  stmt.bindText(4, grid.gridDataJson);
  stmt.bindText(5, grid.source);

  stmt.execute();
}

std::optional<BeatgridRecord> LibraryRepository::getBeatgrid(Database& db, std::int64_t trackId) {
  auto stmt =
      db.prepare("SELECT track_id, bpm, first_beat_frame, grid_data_json, source FROM beatgrids WHERE track_id = ?;");
  stmt.bindInt64(1, trackId);

  if (stmt.step()) {
    BeatgridRecord b;
    b.trackId = stmt.getInt64(0);
    b.bpm = stmt.getDouble(1);
    b.firstBeatFrame = stmt.getInt64(2);
    b.gridDataJson = stmt.getText(3);
    b.source = stmt.getText(4);
    return b;
  }
  return std::nullopt;
}

void LibraryRepository::enqueueAnalysisTask(Database& db, std::int64_t trackId, std::string_view taskType, int version) {
  auto stmt = db.prepare(R"(
    INSERT INTO analysis_tasks (track_id, task_type, status, version, updated_at)
    VALUES (?, ?, 'pending', ?, (strftime('%s', 'now')))
    ON CONFLICT(track_id, task_type) DO UPDATE SET
      status = 'pending',
      version = excluded.version,
      updated_at = (strftime('%s', 'now'));
  )");
  stmt.bindInt64(1, trackId);
  stmt.bindText(2, taskType);
  stmt.bindInt(3, version);
  stmt.execute();
}

std::vector<AnalysisTaskRecord> LibraryRepository::getPendingAnalysisTasks(Database& db, int limit) {
  auto stmt = db.prepare(R"(
    SELECT id, track_id, task_type, status, version, error_message
    FROM analysis_tasks
    WHERE status = 'pending'
    ORDER BY id ASC
    LIMIT ?;
  )");
  stmt.bindInt(1, limit);

  std::vector<AnalysisTaskRecord> tasks;
  while (stmt.step()) {
    AnalysisTaskRecord t;
    t.id = stmt.getInt64(0);
    t.trackId = stmt.getInt64(1);
    t.taskType = stmt.getText(2);
    t.status = stmt.getText(3);
    t.version = stmt.getInt(4);
    t.errorMessage = stmt.getText(5);
    tasks.push_back(t);
  }
  return tasks;
}

void LibraryRepository::updateAnalysisTaskStatus(Database& db, std::int64_t trackId, std::string_view taskType,
                                                 std::string_view status, std::string_view errorMessage) {
  auto stmt = db.prepare(R"(
    UPDATE analysis_tasks
    SET status = ?, error_message = ?, updated_at = (strftime('%s', 'now'))
    WHERE track_id = ? AND task_type = ?;
  )");
  stmt.bindText(1, status);
  stmt.bindText(2, errorMessage);
  stmt.bindInt64(3, trackId);
  stmt.bindText(4, taskType);
  stmt.execute();
}

}  // namespace zyron::library
