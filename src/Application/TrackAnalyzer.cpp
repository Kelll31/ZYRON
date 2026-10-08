// SPDX-License-Identifier: AGPL-3.0-only
#include "Application/TrackAnalyzer.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

#include "AI/Runtime/Tensor.hpp"
#include "Analysis/Bpm/BeatDetector.hpp"
#include "Analysis/Bpm/BeatThisPipeline.hpp"
#include "Analysis/Features/Resampler.hpp"
#include "Analysis/Structure/StructureSegmenter.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Analysis/Energy/EnergyAnalyzer.hpp"
#include "Analysis/Key/KeyDetector.hpp"
#include "Audio/Decoder/JuceAudioFileDecoder.hpp"
#include "Analysis/Loudness/LoudnessMeter.hpp"
#include "Analysis/Structure/MixPointFinder.hpp"
#include "Analysis/Structure/VocalDetector.hpp"
#include "Stems/Cache/StemCache.hpp"
#include "Stems/Demucs/DemucsStemSeparator.hpp"
#include "Application/AnalysisSidecar.hpp"
#include "Library/Database/LibraryRepository.hpp"

namespace zyron::application {

namespace {

constexpr int kAnalysisVersion = 2;  // 2: neural tempo / key (Beat This!, S-KEY) when the weights are present
constexpr int kStructureVersion = 6;  // 6: loudness (LUFS), per-bar energy profile, vocal regions; 5: phrases counted from the first drop
constexpr double kMixTransitionBeats = 32.0;  // the mix-out point leaves room for a transition this long

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
  return text;
}

/// Drum and bass is the default prior (the reference genre of the project); other families come from the genre tag.
analysis::TempoPriorMode priorFor(const std::string& genreTag) {
  const std::string genre = lower(genreTag);
  if (genre.find("house") != std::string::npos || genre.find("techno") != std::string::npos ||
      genre.find("trance") != std::string::npos) {
    return analysis::TempoPriorMode::HouseTechno;
  }
  if (genre.find("hip") != std::string::npos || genre.find("rap") != std::string::npos) {
    return analysis::TempoPriorMode::HipHop;
  }
  return analysis::TempoPriorMode::DnB;
}

constexpr double kExcerptSeconds = 90.0;
constexpr double kDnbMinBpm = 160.0;
constexpr double kDnbMaxBpm = 182.0;

/// Tempo estimators often land on a related pulse (half, double, 3:2). With the drum and bass prior the answer is
/// the related tempo that falls into the genre range; a tempo that already fits is kept.
double foldIntoDrumAndBass(double bpm) {
  if (bpm >= kDnbMinBpm && bpm <= kDnbMaxBpm) {
    return bpm;
  }
  for (const double ratio : {2.0, 0.5, 1.5, 2.0 / 3.0, 4.0 / 3.0, 0.75}) {
    const double candidate = bpm * ratio;
    if (candidate >= kDnbMinBpm && candidate <= kDnbMaxBpm) {
      return candidate;
    }
  }
  return bpm;
}

NeuralModels* g_models = nullptr;  // set once by attach(), before the analysis worker starts
std::filesystem::path g_stemCacheDir;  // <cache>/stems, read only: vocal stems of tracks the user separated

constexpr int kSkeyRate = 22050;

/// Camelot code of each S-KEY output class, in the model's own class order (config.json "keyMap").
constexpr const char* kSkeyCamelot[24] = {
    "11B", "6B", "1B", "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B",  // A, Bb, B, C, C#, D, D#, E, F, F#, G, G# major
    "10A", "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A"};  // B, C, C#, D, D#, E, F, F#, G, G#, A, Bb minor

/// Beat This! on the whole track; returns false (and leaves `out` alone) when the network is not available.
bool neuralBeats(NeuralModels& models, const std::vector<float>& mono, int sampleRate,
                 analysis::TempoPriorMode prior, TrackAnalysis& out, const std::atomic<bool>* cancel) {
  std::string error;
  const auto session = models.beatThis(&error);
  auto filterbank = models.beatThisMelFilterbank(&error);
  if (session == nullptr || filterbank.empty()) {
    return false;
  }
  const analysis::BeatThisPipeline pipeline(
      std::move(filterbank), [session, cancel](const float* spect, float* beat, float* downbeat) {
        if (cancel != nullptr && cancel->load()) {
          return false;  // the application is shutting down: stop between windows
        }
        constexpr std::size_t kFrames = analysis::BeatThisPipeline::kChunkFrames;
        constexpr std::size_t kBins = analysis::BeatThisPipeline::kMelBins;
        ai::Tensor input(ai::TensorShape({1, static_cast<std::int64_t>(kFrames), static_cast<std::int64_t>(kBins)}),
                         std::vector<float>(spect, spect + kFrames * kBins));
        const auto outputs = session->run({input});
        if (outputs.size() < 2 || outputs[0].size() < kFrames || outputs[1].size() < kFrames) {
          return false;
        }
        std::copy_n(outputs[0].data(), kFrames, beat);
        std::copy_n(outputs[1].data(), kFrames, downbeat);
        return true;
      });
  const auto result = pipeline.analyze(mono.data(), mono.size(), sampleRate, prior, &error);
  if (!result.success || result.bpm <= 0.0) {
    return false;
  }
  out.beatOk = true;
  out.bpm = result.bpm;
  out.firstBeatFrame = result.firstBeatFrame;
  out.gridJson = result.gridJson;
  out.beatMethod = "Beat This!";
  return true;
}

/// S-KEY on 90 s from the middle of the track; false when the network is not available.
bool neuralKey(NeuralModels& models, const float* excerpt, std::size_t frames, int sampleRate, TrackAnalysis& out) {
  std::string error;
  const auto session = models.skey(&error);
  if (session == nullptr) {
    return false;
  }
  std::vector<float> audio = analysis::AudioResampler::resample(excerpt, frames, sampleRate, kSkeyRate);
  float peak = 0.0F;
  for (const float sample : audio) {
    peak = std::max(peak, std::abs(sample));
  }
  if (peak <= 1.0e-6F) {
    return false;  // silence
  }
  for (float& sample : audio) {
    sample /= peak;  // the model expects peak-normalised input
  }
  const std::int64_t samples = static_cast<std::int64_t>(audio.size());
  const auto outputs = session->run({ai::Tensor(ai::TensorShape({1, samples}), std::move(audio))});
  if (outputs.empty() || outputs[0].size() < 24) {
    return false;
  }
  std::size_t best = 0;
  for (std::size_t i = 1; i < 24; ++i) {
    if (outputs[0][i] > outputs[0][best]) {
      best = i;
    }
  }
  out.keyOk = true;
  out.camelot = kSkeyCamelot[best];
  out.keyMethod = "S-KEY";
  return true;
}

struct Cache {
  std::mutex mutex;
  std::string path;
  TrackAnalysis result;
  bool valid{false};
};

Cache& cache() {
  static Cache instance;
  return instance;
}

std::unique_ptr<AnalysisSidecar> g_sidecar;  // the analysis file in each music folder (set by attach)

/// Runs the analysis unless the previous call already did it for the same file, or the folder's analysis file
/// already holds it (then nothing is decoded at all, except later for the waveform).
bool cachedAnalysis(const std::string& path, const std::string& contentHash, const std::string& genre,
                    TrackAnalysis& out, std::string& error, const std::atomic<bool>* cancel) {
  Cache& c = cache();
  std::lock_guard<std::mutex> lock(c.mutex);
  if (c.valid && c.path == path) {
    out = c.result;
    return true;
  }
  const std::u8string utf8(reinterpret_cast<const char8_t*>(path.data()), path.size());
  const std::filesystem::path file(utf8);
  int storedStructure = 0;
  if (g_sidecar != nullptr && g_sidecar->load(file, contentHash, kAnalysisVersion, -1, out, &storedStructure)) {
    if (storedStructure != kStructureVersion) {
      // Tempo, key and energy are still valid: only the markers are found again, from the audio, without the networks.
      if (!TrackAnalyzer::reproposeMarkers(file, out, error, g_stemCacheDir, contentHash)) {
        return false;
      }
      g_sidecar->store(file, contentHash, kAnalysisVersion, kStructureVersion, out);
    }
    c.path = path;
    c.result = out;
    c.valid = true;
    return true;
  }
  if (!TrackAnalyzer::analyze(file, genre, out, error, g_models, cancel, g_stemCacheDir, contentHash)) {
    return false;
  }
  if (g_sidecar != nullptr) {
    g_sidecar->store(file, contentHash, kAnalysisVersion, kStructureVersion, out);
  }
  c.path = path;
  c.result = out;
  c.valid = true;
  return true;
}

constexpr double kMaxStemLengthMismatchSec = 2.0;  // a cached stem of another cut of the track is not used

/// Vocal regions from the cached vocal stem of this track, if the user has separated it (read only).
bool vocalRegionsFromStemCache(const std::filesystem::path& stemCacheDir, const std::string& contentHash,
                               double durationSec, const analysis::VocalGrid& grid,
                               std::vector<analysis::VocalRegion>& regions) {
  if (stemCacheDir.empty() || contentHash.empty()) {
    return false;
  }
  try {
    const stems::StemCache cache(stemCacheDir);
    const stems::DemucsStemSeparator separator;
    if (!cache.hasStems(contentHash, separator.modelName(), separator.modelVersion())) {
      return false;
    }
    const auto result = cache.loadStems(contentHash, separator.modelName(), separator.modelVersion());
    if (!result.has_value()) {
      return false;
    }
    const stems::StemBuffer& vocals = result->vocals();
    const double rate = vocals.sampleRate > 0.0 ? vocals.sampleRate : result->sampleRate;
    if (vocals.empty() || rate <= 0.0 ||
        std::abs(static_cast<double>(vocals.numFrames()) / rate - durationSec) > kMaxStemLengthMismatchSec) {
      return false;
    }
    const bool stereo = vocals.right.size() == vocals.left.size();
    std::vector<float> mono(vocals.numFrames());
    for (std::size_t i = 0; i < mono.size(); ++i) {
      mono[i] = 0.5F * (vocals.left[i] + (stereo ? vocals.right[i] : vocals.left[i]));
    }
    regions = analysis::vocalsFromStem(mono.data(), mono.size(), static_cast<int>(rate), grid);
    return true;
  } catch (const std::exception&) {
    return false;  // a damaged cache entry only means no stem: the heuristic runs instead
  }
}

/// Bar energy profile and vocal markers. The grid is the phrase grid of MixPointFinder (bars from mix in), or windows
/// of BarProfile::kNominalBarSec from the audible start when the track has no tempo.
void addBarProfileAndVocals(const std::vector<float>& mono, int sampleRate, const analysis::MixPoints& points,
                            bool hasGrid, double bpm, const std::filesystem::path& stemCacheDir,
                            const std::string& contentHash, TrackAnalysis& out) {
  out.barProfileOk = false;
  out.barProfile = {};
  out.vocalMethod.clear();
  if (points.audibleEndSec <= points.audibleStartSec) {
    return;
  }
  analysis::BarProfileInput input;
  input.mono = mono.data();
  input.frames = mono.size();
  input.sampleRate = sampleRate;
  input.barSec = hasGrid ? 240.0 / bpm : analysis::BarProfile::kNominalBarSec;
  input.originSec = hasGrid && points.mixInSec >= 0.0 ? points.mixInSec : points.audibleStartSec;
  input.endSec = points.audibleEndSec;
  out.barProfile = analysis::computeBarProfile(input);
  out.barProfileOk = !out.barProfile.empty();
  if (!out.barProfileOk) {
    return;
  }

  const analysis::VocalGrid grid{out.barProfile.originSec, out.barProfile.barSec,
                                 static_cast<int>(out.barProfile.energy.size())};
  std::vector<analysis::VocalRegion> regions;
  const double duration = static_cast<double>(mono.size()) / sampleRate;
  if (vocalRegionsFromStemCache(stemCacheDir, contentHash, duration, grid, regions)) {
    out.vocalMethod = "stem";
  } else {
    regions = analysis::detectVocals(mono.data(), mono.size(), sampleRate, grid);
    out.vocalMethod = "DSP";
  }
  for (const analysis::VocalRegion& region : regions) {
    out.markers.push_back({region.startSec, core::TrackMarker::kVocal, "Vocal"});
    out.markers.push_back({region.endSec, core::TrackMarker::kVocalEnd, "Vocal end"});
  }
}

void measureLoudness(const float* left, const float* right, std::size_t frames, int sampleRate, TrackAnalysis& out) {
  const float* channels[2] = {left, right};  // a mono file arrives as the same channel twice
  const analysis::LoudnessResult loudness = analysis::measureIntegratedLoudness(channels, 2, frames, sampleRate);
  out.loudnessOk = loudness.valid;
  out.loudnessLufs = loudness.valid ? loudness.lufs : 0.0;
}

}  // namespace

/// Intro, drops, breakdowns and the outro from the structure analysis, plus the mix points the AI derives from them:
/// where the track may come in (its first beat after the intro starts) and where the mix out should begin (the outro).
void proposeMarkers(const std::vector<float>& mono, int sampleRate, const analysis::EnergyResult& energy,
                    const std::filesystem::path& stemCacheDir, const std::string& contentHash, TrackAnalysis& out) {
  analysis::BeatgridData grid;
  const bool hasGrid = out.beatOk && out.bpm > 0.0;
  if (hasGrid) {
    grid.bpm = out.bpm;
    grid.firstBeatFrame = out.firstBeatFrame;
    grid.sampleRate = sampleRate;
  }
  const analysis::StructureSegmenter segmenter;
  const analysis::TrackStructure structure =
      segmenter.analyze(mono.data(), mono.size(), sampleRate, hasGrid ? &grid : nullptr, &energy);

  // Breakdowns, intro and outro from the segmenter, for the eye. Drops and the mix points come from the bass energy
  // per bar on the phrase grid (MixPointFinder): that is what the Automix acts on.
  for (const analysis::MixPointCue& cue : segmenter.generateMixPointCues(structure)) {
    const char* type = cue.cueType == "break"   ? core::TrackMarker::kBreak
                       : cue.cueType == "intro" ? core::TrackMarker::kIntro
                       : cue.cueType == "outro" ? core::TrackMarker::kOutro
                                                : nullptr;
    if (type != nullptr) {
      out.markers.push_back({cue.timeSec, type, cue.name});
    }
  }

  analysis::MixPointInput input;
  input.mono = mono.data();
  input.frames = mono.size();
  input.sampleRate = sampleRate;
  input.bpm = hasGrid ? out.bpm : 0.0;
  input.firstBeatSec = hasGrid ? static_cast<double>(out.firstBeatFrame) / sampleRate : 0.0;
  input.transitionBeats = kMixTransitionBeats;
  const analysis::MixPoints points = analysis::findMixPoints(input);
  for (std::size_t i = 0; i < points.drops.size(); ++i) {
    out.markers.push_back({points.drops[i], core::TrackMarker::kDrop, i == 0 ? "Drop" : "Drop " + std::to_string(i + 1)});
  }
  if (points.mixInSec >= 0.0) {
    out.markers.push_back({points.mixInSec, core::TrackMarker::kMixIn, "Mix in"});
  }
  if (points.mixOutSec > points.mixInSec) {
    out.markers.push_back({points.mixOutSec, core::TrackMarker::kMixOut, "Mix out"});
  }
  addBarProfileAndVocals(mono, sampleRate, points, hasGrid, out.bpm, stemCacheDir, contentHash, out);
  std::sort(out.markers.begin(), out.markers.end(),
            [](const auto& a, const auto& b) { return a.timeSec < b.timeSec; });
  out.structureOk = !out.markers.empty();
}

bool TrackAnalyzer::reproposeMarkers(const std::filesystem::path& path, TrackAnalysis& out, std::string& error,
                                     const std::filesystem::path& stemCacheDir, const std::string& contentHash) {
  const auto decoded = audio::decodeWithJuce(path, &error);
  if (decoded == nullptr) {
    return false;
  }
  const auto frames = static_cast<std::size_t>(decoded->numFrames());
  const int sampleRate = static_cast<int>(decoded->sampleRate());
  std::vector<float> mono(frames);
  const float* left = decoded->channelData(0);
  const float* right = decoded->channelData(decoded->numChannels() > 1 ? 1 : 0);
  for (std::size_t i = 0; i < frames; ++i) {
    mono[i] = 0.5F * (left[i] + right[i]);
  }
  if (out.sampleRate != sampleRate && out.sampleRate > 0 && out.beatOk) {
    out.firstBeatFrame = static_cast<std::int64_t>(static_cast<double>(out.firstBeatFrame) * sampleRate / out.sampleRate);
  }
  out.sampleRate = sampleRate;
  out.markers.clear();
  measureLoudness(left, right, frames, sampleRate, out);
  const auto energy = analysis::EnergyAnalyzer{}.analyze(mono.data(), frames, sampleRate);
  proposeMarkers(mono, sampleRate, energy, stemCacheDir, contentHash, out);
  // The display peaks come along for free here.
  const float* channels[2] = {left, right};
  out.peaks = analysis::WaveformGenerator::generate(channels, 2, frames, sampleRate);
  out.peaksOk = true;
  return true;
}

bool TrackAnalyzer::computePeaks(const std::filesystem::path& path, TrackAnalysis& out, std::string& error) {
  const auto decoded = audio::decodeWithJuce(path, &error);
  if (decoded == nullptr) {
    return false;
  }
  const float* channels[2] = {decoded->channelData(0), decoded->channelData(decoded->numChannels() > 1 ? 1 : 0)};
  out.peaks = analysis::WaveformGenerator::generate(channels, 2, static_cast<std::size_t>(decoded->numFrames()),
                                                   static_cast<int>(decoded->sampleRate()));
  out.peaksOk = true;
  return true;
}

bool TrackAnalyzer::analyze(const std::filesystem::path& path, const std::string& genre, TrackAnalysis& out,
                            std::string& error, NeuralModels* models, const std::atomic<bool>* cancel,
                            const std::filesystem::path& stemCacheDir, const std::string& contentHash) {
  const auto cancelled = [&] { return cancel != nullptr && cancel->load(); };
  const auto decoded = audio::decodeWithJuce(path, &error);
  if (decoded == nullptr) {
    return false;
  }
  const auto frames = static_cast<std::size_t>(decoded->numFrames());
  const int sampleRate = static_cast<int>(decoded->sampleRate());

  // Display peaks of the whole track, from the decoded audio (any format, unlike the queue's built-in WAV-only task).
  out = TrackAnalysis{};
  {
    const float* channels[2] = {decoded->channelData(0), decoded->channelData(decoded->numChannels() > 1 ? 1 : 0)};
    out.peaks = analysis::WaveformGenerator::generate(channels, 2, frames, sampleRate);
    out.peaksOk = true;
  }

  std::vector<float> mono(frames);
  const float* left = decoded->channelData(0);
  const float* right = decoded->channelData(decoded->numChannels() > 1 ? 1 : 0);
  for (std::size_t i = 0; i < frames; ++i) {
    mono[i] = 0.5F * (left[i] + right[i]);
  }

  out.sampleRate = sampleRate;

  // Tempo and key are stable over a track, so a few minutes of audio add time but not information: analyse the
  // middle of the track. The beat anchor is then a real beat in that middle, which also keeps the error caused by a
  // slightly wrong tempo small around where DJs mix.
  const auto excerptFrames = std::min(frames, static_cast<std::size_t>(kExcerptSeconds * sampleRate));
  const std::size_t excerptStart = (frames - excerptFrames) / 2;
  const float* excerpt = mono.data() + excerptStart;

  const auto prior = priorFor(genre);

  // The neural networks first (they see the whole track for the beat grid); the DSP detectors are the fallback for a
  // machine without the weights or a track the network finds no beat in.
  const bool useNeural = models != nullptr && models->available();
  if (cancelled()) {
    error = "cancelled";
    return false;
  }
  if (!(useNeural && neuralBeats(*models, mono, sampleRate, prior, out, cancel))) {
    if (cancelled()) {
      error = "cancelled";
      return false;
    }
    const auto beats = analysis::BeatDetector{}.detect(excerpt, excerptFrames, sampleRate, prior);
    if (beats.success && beats.bpm > 0.0) {
      out.beatOk = true;
      out.bpm = prior == analysis::TempoPriorMode::DnB ? foldIntoDrumAndBass(beats.bpm) : beats.bpm;
      out.firstBeatFrame = beats.firstBeatFrame + static_cast<std::int64_t>(excerptStart);
      out.gridJson = beats.gridJson;
      out.beatMethod = "DSP";
    }
  }

  if (cancelled()) {
    error = "cancelled";
    return false;
  }
  if (!(useNeural && neuralKey(*models, excerpt, excerptFrames, sampleRate, out))) {
    const auto key = analysis::KeyDetector{}.detectKey(excerpt, excerptFrames, sampleRate);
    if (key.isValid()) {
      out.keyOk = true;
      out.camelot = key.camelot;
      out.keyMethod = "DSP";
    }
  }

  const auto energy = analysis::EnergyAnalyzer{}.analyze(mono.data(), frames, sampleRate);
  out.energyOk = true;
  out.energy = static_cast<double>(energy.globalEnergy);

  if (cancelled()) {
    error = "cancelled";
    return false;
  }
  measureLoudness(left, right, frames, sampleRate, out);
  proposeMarkers(mono, sampleRate, energy, stemCacheDir, contentHash, out);
  return true;
}

void TrackAnalyzer::attach(library::LibraryService& library, const std::filesystem::path& cacheDir,
                           NeuralModels* models) {
  using library::TaskContext;
  g_models = models;
  g_stemCacheDir = cacheDir / "stems";
  // The analysis file lives in the scanned folder that holds the track (the deepest one), else next to the track.
  g_sidecar = std::make_unique<AnalysisSidecar>([&library](const std::filesystem::path& track) {
    std::filesystem::path best;
    for (const auto& folder : library.knownFolders()) {
      const std::u8string utf8(reinterpret_cast<const char8_t*>(folder.data()), folder.size());
      const std::filesystem::path root = std::filesystem::path(utf8).lexically_normal();
      const auto rel = track.lexically_normal().lexically_relative(root);
      if (!rel.empty() && *rel.begin() != ".." && root.native().size() > best.native().size()) {
        best = root;
      }
    }
    return best.empty() ? track.parent_path() : best;
  });

  const auto withTrack = [](const TaskContext& ctx, std::string& error, library::TrackRecord& track,
                            TrackAnalysis& result) {
    if (ctx.db == nullptr) {
      error = "no database connection";
      return false;
    }
    const auto found = library::LibraryRepository::findTrackById(*ctx.db, ctx.trackId);
    if (!found.has_value()) {
      error = "track not found";
      return false;
    }
    track = *found;
    return cachedAnalysis(track.filepath, track.contentHash, track.genre, result, error, ctx.cancelRequested);
  };

  library.registerAnalysisHandler(
      "bpm",
      [withTrack](const TaskContext& ctx, std::string& error) {
        library::TrackRecord track;
        TrackAnalysis result;
        if (!withTrack(ctx, error, track, result)) {
          return false;
        }
        if (!result.beatOk) {
          error = "no steady beat found";
          return false;
        }
        track.bpm = result.bpm;
        library::LibraryRepository::updateTrack(*ctx.db, track);
        return true;
      },
      kAnalysisVersion);

  library.registerAnalysisHandler(
      "beatgrid",
      [withTrack](const TaskContext& ctx, std::string& error) {
        library::TrackRecord track;
        TrackAnalysis result;
        if (!withTrack(ctx, error, track, result)) {
          return false;
        }
        if (!result.beatOk) {
          error = "no steady beat found";
          return false;
        }
        const auto existing = library::LibraryRepository::getBeatgrid(*ctx.db, ctx.trackId);
        if (existing.has_value() && existing->source == "user") {
          return true;  // a grid the user edited is never overwritten (SPEC section 16)
        }
        library::BeatgridRecord grid;
        grid.trackId = ctx.trackId;
        grid.bpm = result.bpm;
        grid.firstBeatFrame = result.firstBeatFrame;
        grid.gridDataJson = result.gridJson;
        grid.source = "auto";
        library::LibraryRepository::saveBeatgrid(*ctx.db, grid);
        return true;
      },
      kAnalysisVersion);

  // Replaces the queue's built-in "waveform" task, which can only read WAV files.
  library.registerAnalysisHandler(
      "waveform",
      [withTrack](const TaskContext& ctx, std::string& error) {
        library::TrackRecord track;
        TrackAnalysis result;
        if (!withTrack(ctx, error, track, result)) {
          return false;
        }
        if (!result.peaksOk) {
          // The analysis came from the folder's analysis file: only the display peaks need the audio.
          const std::u8string utf8(reinterpret_cast<const char8_t*>(track.filepath.data()), track.filepath.size());
          if (!TrackAnalyzer::computePeaks(std::filesystem::path(utf8), result, error)) {
            return false;
          }
        }
        const auto folder = ctx.cacheDirectory / "waveforms";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        const std::string name = track.contentHash.empty() ? "track_" + std::to_string(track.id) : track.contentHash;
        const auto path = folder / (name + ".zywv");
        if (!result.peaks.saveToFile(path, &error)) {
          return false;
        }
        const std::u8string utf8 = path.u8string();
        track.waveformPeaksPath = std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        library::LibraryRepository::updateTrack(*ctx.db, track);
        return true;
      },
      kAnalysisVersion);

  library.registerAnalysisHandler(
      "key",
      [withTrack](const TaskContext& ctx, std::string& error) {
        library::TrackRecord track;
        TrackAnalysis result;
        if (!withTrack(ctx, error, track, result)) {
          return false;
        }
        if (!result.keyOk) {
          error = "no clear key found";
          return false;
        }
        track.key = result.camelot;
        library::LibraryRepository::updateTrack(*ctx.db, track);
        return true;
      },
      kAnalysisVersion);

  library.registerAnalysisHandler(
      "energy",
      [withTrack](const TaskContext& ctx, std::string& error) {
        library::TrackRecord track;
        TrackAnalysis result;
        if (!withTrack(ctx, error, track, result)) {
          return false;
        }
        track.energy = result.energy;
        library::LibraryRepository::updateTrack(*ctx.db, track);
        return true;
      },
      kAnalysisVersion);

  library.registerAnalysisHandler(
      "structure",
      [withTrack](const TaskContext& ctx, std::string& error) {
        library::TrackRecord track;
        TrackAnalysis result;
        if (!withTrack(ctx, error, track, result)) {
          return false;
        }
        // Loudness and the bar profile live on the track row (they are not markers).
        track.loudnessLufs = result.loudnessOk ? result.loudnessLufs : 0.0;
        track.barProfile = result.barProfileOk ? result.barProfile.encode() : std::string{};
        library::LibraryRepository::updateTrack(*ctx.db, track);
        if (!result.structureOk) {
          return true;  // nothing to propose for this track (too uniform): finding no structure is not a failure
        }
        // The AI's proposals replace its earlier ones; the user's own markers are never touched.
        std::set<int> taken;
        for (const auto& cue : library::LibraryRepository::getCuePoints(*ctx.db, ctx.trackId)) {
          if (cue.source == "user") {
            taken.insert(cue.index);
          }
        }
        library::LibraryRepository::removeAutoCues(*ctx.db, ctx.trackId, library::LibraryService::kFirstMarkerIndex);
        int index = library::LibraryService::kFirstMarkerIndex;
        for (const auto& marker : result.markers) {
          while (taken.count(index) > 0) {
            ++index;
          }
          library::CuePointRecord cue;
          cue.trackId = ctx.trackId;
          cue.index = index++;
          cue.frame = static_cast<std::int64_t>(marker.timeSec * result.sampleRate);
          cue.name = marker.name;
          cue.type = marker.type;
          cue.source = "auto";
          library::LibraryRepository::saveCuePoint(*ctx.db, cue);
        }
        return true;
      },
      kStructureVersion);

  library.startAnalysis(cacheDir);
}

}  // namespace zyron::application
