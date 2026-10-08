// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Database/Migrations.hpp"

#include <stdexcept>
#include <string>

namespace zyron::library {

namespace {

void migrationV1(Database& db) {
  // 1. Tracks table
  db.execute(R"(
    CREATE TABLE IF NOT EXISTS tracks (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      filepath TEXT NOT NULL UNIQUE,
      content_hash TEXT NOT NULL,
      file_size INTEGER NOT NULL DEFAULT 0,
      file_mtime INTEGER NOT NULL DEFAULT 0,
      title TEXT NOT NULL DEFAULT '',
      artist TEXT NOT NULL DEFAULT '',
      album TEXT NOT NULL DEFAULT '',
      genre TEXT NOT NULL DEFAULT '',
      year INTEGER NOT NULL DEFAULT 0,
      bpm REAL NOT NULL DEFAULT 0.0,
      key TEXT NOT NULL DEFAULT '',
      energy REAL NOT NULL DEFAULT 0.0,
      duration_sec REAL NOT NULL DEFAULT 0.0,
      sample_rate INTEGER NOT NULL DEFAULT 0,
      channels INTEGER NOT NULL DEFAULT 0,
      waveform_peaks_path TEXT NOT NULL DEFAULT '',
      stem_status TEXT NOT NULL DEFAULT 'none',
      created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
      updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
    );

    CREATE INDEX IF NOT EXISTS idx_tracks_content_hash ON tracks(content_hash);
    CREATE INDEX IF NOT EXISTS idx_tracks_artist_title ON tracks(artist, title);
    CREATE INDEX IF NOT EXISTS idx_tracks_bpm ON tracks(bpm);
    CREATE INDEX IF NOT EXISTS idx_tracks_key ON tracks(key);
  )");

  // 2. Playlists table & join table
  db.execute(R"(
    CREATE TABLE IF NOT EXISTS playlists (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      name TEXT NOT NULL UNIQUE,
      created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
      updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
    );

    CREATE TABLE IF NOT EXISTS playlist_tracks (
      playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
      track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
      position INTEGER NOT NULL,
      PRIMARY KEY (playlist_id, position)
    );
  )");

  // 3. Cue points (hot cues 1..8)
  db.execute(R"(
    CREATE TABLE IF NOT EXISTS cue_points (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
      cue_index INTEGER NOT NULL,
      frame INTEGER NOT NULL,
      name TEXT NOT NULL DEFAULT '',
      color TEXT NOT NULL DEFAULT '',
      type TEXT NOT NULL DEFAULT 'cue',
      source TEXT NOT NULL DEFAULT 'auto',
      UNIQUE (track_id, cue_index)
    );
  )");

  // 4. Beatgrids
  db.execute(R"(
    CREATE TABLE IF NOT EXISTS beatgrids (
      track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
      bpm REAL NOT NULL,
      first_beat_frame INTEGER NOT NULL,
      grid_data_json TEXT NOT NULL DEFAULT '',
      source TEXT NOT NULL DEFAULT 'auto'
    );
  )");

  // 5. Analysis task queue
  db.execute(R"(
    CREATE TABLE IF NOT EXISTS analysis_tasks (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
      task_type TEXT NOT NULL,
      status TEXT NOT NULL DEFAULT 'pending',
      version INTEGER NOT NULL DEFAULT 1,
      error_message TEXT NOT NULL DEFAULT '',
      updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
      UNIQUE (track_id, task_type)
    );
  )");

  // 6. Full-Text Search (FTS5) & sync triggers
  db.execute(R"(
    CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5(
      title,
      artist,
      album,
      genre,
      filepath,
      content='tracks',
      content_rowid='id'
    );

    CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN
      INSERT INTO tracks_fts(rowid, title, artist, album, genre, filepath)
      VALUES (new.id, new.title, new.artist, new.album, new.genre, new.filepath);
    END;

    CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN
      INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, genre, filepath)
      VALUES ('delete', old.id, old.title, old.artist, old.album, old.genre, old.filepath);
    END;

    CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE ON tracks BEGIN
      INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, genre, filepath)
      VALUES ('delete', old.id, old.title, old.artist, old.album, old.genre, old.filepath);
      INSERT INTO tracks_fts(rowid, title, artist, album, genre, filepath)
      VALUES (new.id, new.title, new.artist, new.album, new.genre, new.filepath);
    END;
  )");
}

}  // namespace

void Migrations::apply(Database& db) {
  const int initialVersion = db.userVersion();
  if (initialVersion >= kCurrentSchemaVersion) {
    return;
  }

  for (int v = initialVersion + 1; v <= kCurrentSchemaVersion; ++v) {
    db.beginTransaction();
    try {
      switch (v) {
        case 1:
          migrationV1(db);
          break;
        default:
          throw std::runtime_error("Unknown migration version: " + std::to_string(v));
      }
      db.setUserVersion(v);
      db.commit();
    } catch (...) {
      db.rollback();
      throw;
    }
  }
}

}  // namespace zyron::library
