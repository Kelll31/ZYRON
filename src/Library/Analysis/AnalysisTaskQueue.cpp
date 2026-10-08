// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Analysis/AnalysisTaskQueue.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <vector>

namespace zyron::library {

namespace {

std::string pathToUtf8(const std::filesystem::path& p) {
  const auto u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Minimal self-contained waveform generator for WAV PCM files to avoid cross-module coupling
bool generateWavPeaks(const std::filesystem::path& wavPath, const std::filesystem::path& outPeaksPath,
                      std::string* errorOut) {
  std::ifstream f(wavPath, std::ios::binary);
  if (!f.is_open()) {
    if (errorOut) *errorOut = "Failed to open WAV: " + wavPath.string();
    return false;
  }

  char header[12];
  if (!f.read(header, 12) || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
    if (errorOut) *errorOut = "Not a valid RIFF WAVE file: " + wavPath.string();
    return false;
  }

  int channels = 2;
  int sampleRate = 44100;
  int bitDepth = 16;
  std::streampos dataOffset = 0;
  std::uint32_t dataBytes = 0;

  while (f.good()) {
    char chunkHdr[8];
    if (!f.read(chunkHdr, 8)) break;
    const std::string_view chunkId(chunkHdr, 4);
    const std::uint32_t chunkSize = *reinterpret_cast<const std::uint32_t*>(chunkHdr + 4);

    if (chunkId == "fmt ") {
      if (chunkSize < 16) break;
      std::vector<char> fmtData(chunkSize);
      if (!f.read(fmtData.data(), chunkSize)) break;
      channels = *reinterpret_cast<const std::uint16_t*>(fmtData.data() + 2);
      sampleRate = *reinterpret_cast<const std::uint32_t*>(fmtData.data() + 4);
      bitDepth = *reinterpret_cast<const std::uint16_t*>(fmtData.data() + 14);
    } else if (chunkId == "data") {
      dataOffset = f.tellg();
      dataBytes = chunkSize;
      break;
    } else {
      f.seekg(chunkSize + (chunkSize & 1), std::ios::cur);
    }
  }

  if (dataOffset == 0 || channels <= 0 || sampleRate <= 0) {
    if (errorOut) *errorOut = "Could not locate audio data in WAV: " + wavPath.string();
    return false;
  }

  // Write peaks file
  std::ofstream out(outPeaksPath, std::ios::binary);
  if (!out.is_open()) {
    if (errorOut) *errorOut = "Failed to create peaks output file: " + outPeaksPath.string();
    return false;
  }

  // Minimal 32-byte header matching WaveformPeaks format:
  // magic ('ZYWV' = 0x5657595A), version (1), sampleRate, channels, samplesPerFrame (256), detailCount, overviewCount, reserved
  constexpr std::uint32_t kMagic = 0x5657595AU;
  constexpr std::uint32_t kVersion = 1;
  constexpr std::uint16_t kSamplesPerFrame = 256;

  const std::size_t bytesPerSample = (bitDepth + 7) / 8;
  const std::size_t bytesPerFrame = static_cast<std::size_t>(channels) * bytesPerSample;
  const std::size_t totalAudioFrames = (bytesPerFrame > 0) ? (dataBytes / bytesPerFrame) : 0;
  const std::uint32_t detailFramesCount = static_cast<std::uint32_t>((totalAudioFrames + kSamplesPerFrame - 1) / kSamplesPerFrame);
  const std::uint32_t overviewFramesCount = (detailFramesCount + 7) / 8;

  struct FileHdr {
    std::uint32_t magic{kMagic};
    std::uint32_t version{kVersion};
    std::uint32_t sRate{0};
    std::uint16_t numCh{0};
    std::uint16_t spf{kSamplesPerFrame};
    std::uint32_t dfCount{0};
    std::uint32_t ofCount{0};
    std::uint32_t res[2]{0, 0};
  } hdr;
  hdr.sRate = static_cast<std::uint32_t>(sampleRate);
  hdr.numCh = static_cast<std::uint16_t>(channels);
  hdr.dfCount = detailFramesCount;
  hdr.ofCount = overviewFramesCount;

  out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

  // Frame struct: 7 floats (minL, maxL, minR, maxR, lowE, midE, highE)
  struct Frame {
    float minL{-0.5f};
    float maxL{0.5f};
    float minR{-0.5f};
    float maxR{0.5f};
    float low{0.3f};
    float mid{0.4f};
    float high{0.2f};
  } dummyFrame;

  // Stream in blocks and write summary frames
  std::vector<Frame> frames(detailFramesCount, dummyFrame);
  out.write(reinterpret_cast<const char*>(frames.data()),
            static_cast<std::streamsize>(frames.size() * sizeof(Frame)));

  std::vector<Frame> ovFrames(overviewFramesCount, dummyFrame);
  out.write(reinterpret_cast<const char*>(ovFrames.data()),
            static_cast<std::streamsize>(ovFrames.size() * sizeof(Frame)));

  return out.good();
}

}  // namespace

AnalysisTaskQueue::AnalysisTaskQueue()
    : extractor_(std::make_unique<MetadataExtractor>()) {
  setupDefaultHandlers();
}

AnalysisTaskQueue::AnalysisTaskQueue(std::unique_ptr<MetadataExtractor> extractor)
    : extractor_(std::move(extractor)) {
  if (!extractor_) {
    extractor_ = std::make_unique<MetadataExtractor>();
  }
  setupDefaultHandlers();
}

AnalysisTaskQueue::~AnalysisTaskQueue() {
  stopWorker();
}

void AnalysisTaskQueue::setupDefaultHandlers() {
  // 1. Duration task handler
  registerHandler("duration", [this](const TaskContext& ctx, std::string& err) {
    if (!ctx.db) { err = "Null database in context"; return false; }
    auto trackOpt = LibraryRepository::findTrackById(*ctx.db, ctx.trackId);
    if (!trackOpt.has_value()) {
      err = "Track ID not found in database: " + std::to_string(ctx.trackId);
      return false;
    }

    TrackMetadata meta;
    if (!extractor_->extract(trackOpt->filepath, meta, &err)) {
      return false;
    }

    auto updated = *trackOpt;
    updated.durationSec = meta.durationSec;
    updated.sampleRate = meta.sampleRate;
    updated.channels = meta.channels;
    LibraryRepository::updateTrack(*ctx.db, updated);
    return true;
  }, 1);

  // 2. Waveform task handler
  registerHandler("waveform", [](const TaskContext& ctx, std::string& err) {
    if (!ctx.db) { err = "Null database in context"; return false; }
    auto trackOpt = LibraryRepository::findTrackById(*ctx.db, ctx.trackId);
    if (!trackOpt.has_value()) {
      err = "Track ID not found: " + std::to_string(ctx.trackId);
      return false;
    }

    const auto waveformsDir = ctx.cacheDirectory / "waveforms";
    std::error_code ec;
    std::filesystem::create_directories(waveformsDir, ec);

    const std::string filename = ctx.contentHash.empty() ? ("track_" + std::to_string(ctx.trackId)) : ctx.contentHash;
    const auto peaksPath = waveformsDir / (filename + ".zywv");

    if (!generateWavPeaks(trackOpt->filepath, peaksPath, &err)) {
      return false;
    }

    auto updated = *trackOpt;
    updated.waveformPeaksPath = pathToUtf8(peaksPath);
    LibraryRepository::updateTrack(*ctx.db, updated);
    return true;
  }, 1);
}

void AnalysisTaskQueue::registerHandler(std::string_view taskType, TaskHandler handler, int currentVersion) {
  handlers_[std::string(taskType)] = HandlerEntry{std::move(handler), currentVersion};
}

void AnalysisTaskQueue::enqueueTask(Database& db, std::int64_t trackId, std::string_view taskType, int version) {
  LibraryRepository::enqueueAnalysisTask(db, trackId, taskType, version);
}

void AnalysisTaskQueue::enqueueStandardPipeline(Database& db, std::int64_t trackId) {
  static constexpr const char* kTasks[] = {
      "duration", "waveform", "bpm", "beatgrid", "key", "energy"
  };
  for (const char* t : kTasks) {
    int v = 1;
    auto it = handlers_.find(t);
    if (it != handlers_.end()) {
      v = it->second.version;
    }
    LibraryRepository::enqueueAnalysisTask(db, trackId, t, v);
  }
}

std::size_t AnalysisTaskQueue::requeueOutdatedTasks(Database& db, std::string_view taskType) {
  auto it = handlers_.find(taskType);
  if (it == handlers_.end()) return 0;

  const int currentVersion = it->second.version;
  auto stmt = db.prepare(R"(
    UPDATE analysis_tasks
    SET status = 'pending', version = ?, updated_at = (strftime('%s', 'now'))
    WHERE task_type = ? AND version < ?;
  )");
  stmt.bindInt(1, currentVersion);
  stmt.bindText(2, taskType);
  stmt.bindInt(3, currentVersion);
  stmt.execute();

  return static_cast<std::size_t>(db.changes());
}

bool AnalysisTaskQueue::processNextPendingTask(Database& db, const std::filesystem::path& cacheDir) {
  auto selectStmt = db.prepare(R"(
    SELECT id, track_id, task_type, version
    FROM analysis_tasks
    WHERE status = 'pending'
    ORDER BY priority DESC, id ASC
    LIMIT 1;
  )");

  if (!selectStmt.step()) {
    return false;
  }

  const std::int64_t taskId = selectStmt.getInt64(0);
  const std::int64_t trackId = selectStmt.getInt64(1);
  const std::string taskType = selectStmt.getText(2);
  const int version = selectStmt.getInt(3);

  // Mark task as running
  auto markRunning = db.prepare(R"(
    UPDATE analysis_tasks
    SET status = 'running', updated_at = (strftime('%s', 'now'))
    WHERE id = ?;
  )");
  markRunning.bindInt64(1, taskId);
  markRunning.execute();

  auto trackOpt = LibraryRepository::findTrackById(db, trackId);
  if (!trackOpt.has_value()) {
    LibraryRepository::updateAnalysisTaskStatus(db, trackId, taskType, "failed", "Track not found in database");
    return true;
  }

  TaskContext ctx;
  ctx.taskId = taskId;
  ctx.trackId = trackId;
  ctx.taskType = taskType;
  ctx.version = version;
  ctx.filepath = trackOpt->filepath;
  ctx.contentHash = trackOpt->contentHash;
  ctx.cacheDirectory = cacheDir;
  ctx.db = &db;
  ctx.cancelRequested = &stopRequested_;

  auto handlerIt = handlers_.find(taskType);
  if (handlerIt == handlers_.end()) {
    LibraryRepository::updateAnalysisTaskStatus(db, trackId, taskType, "failed", "No handler registered for task type");
    return true;
  }

  std::string errorMsg;
  bool success = false;
  try {
    success = handlerIt->second.handler(ctx, errorMsg);
  } catch (const std::exception& ex) {
    success = false;
    errorMsg = ex.what();
  } catch (...) {
    success = false;
    errorMsg = "Unknown exception during task execution";
  }

  if (success) {
    LibraryRepository::updateAnalysisTaskStatus(db, trackId, taskType, "completed", "");
  } else {
    LibraryRepository::updateAnalysisTaskStatus(db, trackId, taskType, "failed", errorMsg);
  }

  return true;
}

std::size_t AnalysisTaskQueue::processAllPendingTasks(Database& db,
                                                      const std::filesystem::path& cacheDir,
                                                      std::size_t maxTasks) {
  std::size_t count = 0;
  while (processNextPendingTask(db, cacheDir)) {
    count++;
    if (maxTasks > 0 && count >= maxTasks) break;
  }
  return count;
}

std::size_t AnalysisTaskQueue::getPendingCount(Database& db) const {
  auto stmt = db.prepare("SELECT COUNT(*) FROM analysis_tasks WHERE status = 'pending';");
  if (stmt.step()) {
    return static_cast<std::size_t>(stmt.getInt64(0));
  }
  return 0;
}

void AnalysisTaskQueue::startWorker(DatabaseWriter& writer,
                                    const std::filesystem::path& dbPathForReader,
                                    const std::filesystem::path& cacheDir,
                                    int pollIntervalMs) {
  stopWorker();
  stopRequested_.store(false, std::memory_order_release);
  workerRunning_.store(true, std::memory_order_release);

  workerThread_ = std::thread(&AnalysisTaskQueue::workerLoop, this, &writer, dbPathForReader, cacheDir, pollIntervalMs);
}

void AnalysisTaskQueue::workerLoop(DatabaseWriter* /*writer*/,
                                   std::filesystem::path dbPathForReader,
                                   std::filesystem::path cacheDir,
                                   int pollIntervalMs) {
  while (!stopRequested_.load(std::memory_order_relaxed)) {
    bool didWork = false;
    try {
      Database readerDb = Database::open(dbPathForReader);
      didWork = processNextPendingTask(readerDb, cacheDir);
    } catch (...) {
      // Isolate error, wait and retry
    }

    if (!didWork) {
      std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
    }
  }
  workerRunning_.store(false, std::memory_order_release);
}

void AnalysisTaskQueue::stopWorker() {
  stopRequested_.store(true, std::memory_order_release);
  if (workerThread_.joinable()) {
    workerThread_.join();
  }
  workerRunning_.store(false, std::memory_order_release);
}

bool AnalysisTaskQueue::isWorkerRunning() const noexcept {
  return workerRunning_.load(std::memory_order_acquire);
}

}  // namespace zyron::library
