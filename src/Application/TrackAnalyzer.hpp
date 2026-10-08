// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "Analysis/Structure/BarProfile.hpp"
#include "Analysis/Waveform/WaveformPeaks.hpp"
#include "Application/NeuralModels.hpp"
#include "Library/LibraryService.hpp"

namespace zyron::application {

/// What one decode-and-analyse pass learns about a track (SPEC sections 15, 16, 31, 53).
struct TrackAnalysis {
  bool beatOk{false};
  double bpm{0.0};
  std::int64_t firstBeatFrame{0};
  int sampleRate{0};
  std::string gridJson;

  bool keyOk{false};
  std::string camelot;

  bool energyOk{false};
  double energy{0.0};

  /// Structure of the track as the AI proposes it: intro, drops, breakdowns, outro and the mix points derived from them.
  struct AutoMarker {
    double timeSec{0.0};
    std::string type;  // core::TrackMarker::k... names
    std::string name;
  };
  bool peaksOk{false};
  analysis::WaveformPeaks peaks;  // display peaks of the whole track (cached as .zywv)

  bool structureOk{false};
  std::vector<AutoMarker> markers;

  /// Integrated loudness (ITU-R BS.1770 / EBU R128, LUFS) of the whole track; not valid for silence or < 400 ms.
  bool loudnessOk{false};
  double loudnessLufs{0.0};

  /// Energy of each bar on the phrase grid (0..1), for matching energy at transitions.
  bool barProfileOk{false};
  analysis::BarProfile barProfile;

  /// How the vocal regions were found: "stem" (separated vocal stem in the cache), "DSP" (heuristic) or "" (none run).
  /// The regions themselves are markers: core::TrackMarker::kVocal at the start, kVocalEnd at the end.
  std::string vocalMethod;

  std::string beatMethod;  // "Beat This!" or "DSP"
  std::string keyMethod;   // "S-KEY" or "DSP"
};

/// Decodes a track once and runs the DSP analysers on it: tempo and beat grid, key, energy. Registered as the handlers
/// of the library's analysis queue, which calls them for "bpm", "beatgrid", "key" and "energy" tasks of the same track
/// one after another; the last result is cached so the file is decoded once, not four times.
class TrackAnalyzer {
 public:
  /// Registers the four handlers on `library` and starts its background analysis worker.
  /// With `models`, tempo and key come from the neural networks (Beat This!, S-KEY) whenever the weights are present;
  /// the DSP detectors are the fallback. `models` must outlive the library's analysis worker.
  static void attach(library::LibraryService& library, const std::filesystem::path& cacheDir,
                     NeuralModels* models = nullptr);

  /// Finds the markers again from the audio, keeping tempo, key and energy (a newer marker finder, no networks). Also
  /// measures loudness and the bar energy profile and finds the vocal regions (using the cached vocal stem when there is
  /// one in `stemCacheDir`).
  [[nodiscard]] static bool reproposeMarkers(const std::filesystem::path& path, TrackAnalysis& out, std::string& error,
                                             const std::filesystem::path& stemCacheDir = {},
                                             const std::string& contentHash = {});

  /// Decodes `path` for its display peaks only (when the rest of the analysis came from the folder's analysis file).
  [[nodiscard]] static bool computePeaks(const std::filesystem::path& path, TrackAnalysis& out, std::string& error);

  /// Decodes and analyses `path`. `genre` picks the tempo prior (drum and bass is the default, ADR-0010).
  [[nodiscard]] static bool analyze(const std::filesystem::path& path, const std::string& genre, TrackAnalysis& out,
                                    std::string& error, NeuralModels* models = nullptr,
                                    const std::atomic<bool>* cancel = nullptr,
                                    const std::filesystem::path& stemCacheDir = {}, const std::string& contentHash = {});
};

}  // namespace zyron::application
