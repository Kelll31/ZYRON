// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "Application/TrackAnalyzer.hpp"

namespace zyron::application {

/// The analysis of a music folder, saved in that folder as `zyron-analysis.json`: tempo and beat grid, key, energy,
/// drops and mix points of every analysed track, keyed by the file's content hash (a renamed or moved file keeps its
/// analysis). A fresh library, another computer or a reinstall reads it instead of running the neural networks again.
///
/// Thread-safety: all methods may be called from any thread; they serialise on an internal mutex.
class AnalysisSidecar {
 public:
  static constexpr const char* kFileName = "zyron-analysis.json";

  /// Maps a track file to the folder whose sidecar holds it (the scanned library folder that contains it).
  using FolderResolver = std::function<std::filesystem::path(const std::filesystem::path& track)>;

  explicit AnalysisSidecar(FolderResolver resolver);
  ~AnalysisSidecar();

  /// Fills `out` from the sidecar when it holds this file at the current analysis versions. Waveform peaks are not
  /// stored (they are cheap to recompute and large): `out.peaksOk` stays false.
  /// `structureVersion` < 0 accepts any stored structure; `storedStructureVersion` (optional) tells which it was, so a
  /// newer marker finder can rerun on the audio without the neural networks.
  [[nodiscard]] bool load(const std::filesystem::path& track, const std::string& contentHash, int analysisVersion,
                          int structureVersion, TrackAnalysis& out, int* storedStructureVersion = nullptr);

  /// Records a fresh analysis and rewrites the folder's file (atomically: a crash never leaves half a file).
  void store(const std::filesystem::path& track, const std::string& contentHash, int analysisVersion,
             int structureVersion, const TrackAnalysis& analysis);

 private:
  struct Folder;
  Folder& folderFor(const std::filesystem::path& folder);

  FolderResolver resolver_;
  std::mutex mutex_;
  std::map<std::filesystem::path, std::unique_ptr<Folder>> folders_;
};

}  // namespace zyron::application
