// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zyron::core {

/// Normalized summary of a library track (SPEC sections 28, 30).
struct TrackItem {
  std::int64_t id{0};
  std::string filepath;
  std::string contentHash;
  std::string title;
  std::string artist;
  std::string album;
  std::string genre;
  int year{0};
  double bpm{0.0};
  double firstBeatSec{0.0};  // first beat of the beat grid; meaningful only when bpm > 0
  std::string key;
  double energy{0.0};
  double loudnessLufs{0.0};  // integrated loudness (ITU-R BS.1770 / EBU R128); 0.0 = not measured, real values are < 0
  double durationSec{0.0};
  std::string waveformPeaksPath;
  std::string stemStatus{"none"};
  // How far the background preparation of the track has got (tempo, grid, key, energy, structure, ...).
  int analysisDone{0};    // finished tasks, failed ones included
  int analysisFailed{0};  // of those, tasks that failed
  int analysisTotal{0};   // all tasks queued for the track (0: none known)

  [[nodiscard]] std::string formatDuration() const {
    const int totalSec = static_cast<int>(durationSec);
    const int min = totalSec / 60;
    const int sec = totalSec % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", min, sec);
    return std::string(buf);
  }
};

/// A point of a track the DJ (or the AI) cares about: where a mix may start or end, the drop, a breakdown (SPEC
/// sections 25, 54, 57). The AI proposes them from the track structure (source "auto"); the user's own edits (source
/// "user") are never overwritten by a later analysis.
struct TrackMarker {
  static constexpr const char* kMixIn = "mix_in";    // where this track may come in
  static constexpr const char* kMixOut = "mix_out";  // where the mix out of this track should begin
  static constexpr const char* kDrop = "drop";
  static constexpr const char* kBreak = "break";
  static constexpr const char* kIntro = "intro";
  static constexpr const char* kOutro = "outro";
  /// A vocal section is a pair of markers: kVocal at its start and kVocalEnd at its end (in time order, never nested;
  /// an unmatched kVocal runs to the end of the track).
  static constexpr const char* kVocal = "vocal";
  static constexpr const char* kVocalEnd = "vocal_end";

  int id{0};                // 0: a new marker
  double timeSec{0.0};
  std::string type;         // one of the k... names above
  std::string name;         // optional label
  std::string source{"user"};  // "auto" (AI) or "user"
};

/// Progress of the background folder scan, for a status line in the library panel.
struct LibraryScanStatus {
  bool scanning{false};
  std::size_t discovered{0};
  std::size_t processed{0};
  std::size_t errors{0};
  std::size_t analysisPending{0};  // tracks still waiting for tempo / key / energy analysis
  std::string currentFile;
  std::string lastError;  // set when a scan could not run at all (unreadable folder, database problem)
};

/// Library query and scanner trigger interface (SPEC section 30, ARCHITECTURE section 9).
/// Allows UI to search, list, and trigger library scanning without direct dependency on SQLite or scanner threads.
class ILibrarySource {
 public:
  virtual ~ILibrarySource() = default;

  [[nodiscard]] virtual std::vector<TrackItem> search(std::string_view query) = 0;
  [[nodiscard]] virtual std::vector<TrackItem> listAll() = 0;
  /// Queues a folder for scanning and returns at once; the scan runs on a background thread.
  virtual void requestScan(const std::string& folderPath) = 0;

  /// The track with this id, if the library knows it. Safe from any thread.
  [[nodiscard]] virtual std::optional<TrackItem> findTrack(std::int64_t id) {
    (void)id;
    return std::nullopt;
  }
  [[nodiscard]] virtual LibraryScanStatus scanStatus() const { return {}; }

  /// The markers of a track, ordered by time.
  [[nodiscard]] virtual std::vector<TrackMarker> markers(std::int64_t trackId) {
    (void)trackId;
    return {};
  }
  /// Adds a marker (id 0) or replaces the one with that id; returns its id, or 0 on failure. Stored as the user's.
  virtual int setMarker(std::int64_t trackId, const TrackMarker& marker) {
    (void)trackId;
    (void)marker;
    return 0;
  }
  virtual void removeMarker(std::int64_t trackId, int markerId) {
    (void)trackId;
    (void)markerId;
  }

  /// Puts the background preparation of a track (tempo, grid, key, structure, ...) at the front of the queue; tasks that
  /// failed before are tried again.
  virtual void prioritizeAnalysis(std::int64_t trackId) { (void)trackId; }
  /// Runs the whole analysis of a track again (the AI's markers are replaced, the user's stay) at the front of the queue.
  virtual void reanalyze(std::int64_t trackId) { (void)trackId; }
  /// Takes the track out of the library (the file on disk is not touched).
  virtual void removeTrack(std::int64_t trackId) { (void)trackId; }
};

}  // namespace zyron::core
