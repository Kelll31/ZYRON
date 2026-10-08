// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
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
  std::string key;
  double energy{0.0};
  double durationSec{0.0};
  std::string waveformPeaksPath;
  std::string stemStatus{"none"};

  [[nodiscard]] std::string formatDuration() const {
    const int totalSec = static_cast<int>(durationSec);
    const int min = totalSec / 60;
    const int sec = totalSec % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", min, sec);
    return std::string(buf);
  }
};

/// Library query and scanner trigger interface (SPEC section 30, ARCHITECTURE section 9).
/// Allows UI to search, list, and trigger library scanning without direct dependency on SQLite or scanner threads.
class ILibrarySource {
 public:
  virtual ~ILibrarySource() = default;

  [[nodiscard]] virtual std::vector<TrackItem> search(std::string_view query) = 0;
  [[nodiscard]] virtual std::vector<TrackItem> listAll() = 0;
  virtual void requestScan(const std::string& folderPath) = 0;
};

}  // namespace zyron::core
