// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Deck/TrackLoader.hpp"

#include <utility>

#include "Audio/Decoder/WavDecoder.hpp"

namespace zyron::audio {

TrackLoader::TrackLoader() {
  start();
}

TrackLoader::~TrackLoader() {
  stop();
}

void TrackLoader::start() {
  if (!running_.load(std::memory_order_relaxed)) {
    running_.store(true, std::memory_order_release);
    workerThread_ = std::thread(&TrackLoader::workerLoop, this);
  }
}

void TrackLoader::stop() {
  if (running_.exchange(false, std::memory_order_acq_rel)) {
    queueCv_.notify_all();
    if (workerThread_.joinable()) {
      workerThread_.join();
    }
  }
  purgeRetiredBuffers();
}

std::shared_ptr<TrackBuffer> TrackLoader::decodeFile(const std::filesystem::path& path, std::string* errorOut) {
  // Primary RIFF/WAVE decoder (SPEC section 27)
  auto buffer = WavDecoder::decode(path, errorOut);
  if (buffer != nullptr) {
    return buffer;
  }

  if (errorOut != nullptr && errorOut->empty()) {
    *errorOut = "Unsupported or unreadable audio format: " + path.extension().string();
  }
  return nullptr;
}

TrackLoadResult TrackLoader::loadTrackSync(core::DeckId deck, const std::filesystem::path& path, DeckPlayer& player) {
  TrackLoadResult result;
  result.deck = deck;
  result.path = path;

  std::string error;
  auto newBuffer = decodeFile(path, &error);
  if (newBuffer == nullptr) {
    result.success = false;
    result.errorMessage = error;
    return result;
  }

  // Atomic hand-off to DeckPlayer: capture previously active buffer for deferred retirement
  auto oldBuffer = player.currentTrack();
  player.loadTrack(newBuffer);

  if (oldBuffer != nullptr) {
    retireBuffer(std::move(oldBuffer));
  }

  result.success = true;
  result.buffer = std::move(newBuffer);
  return result;
}

void TrackLoader::loadTrackAsync(core::DeckId deck, const std::filesystem::path& path, DeckPlayer& player,
                                 CompletionCallback callback) {
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    tasks_.push_back(LoadTask{deck, path, &player, std::move(callback)});
  }
  queueCv_.notify_one();
}

void TrackLoader::unloadTrack(core::DeckId deck, DeckPlayer& player) {
  (void)deck;
  auto oldBuffer = player.currentTrack();
  player.unloadTrack();

  if (oldBuffer != nullptr) {
    retireBuffer(std::move(oldBuffer));
  }
}

void TrackLoader::retireBuffer(std::shared_ptr<const TrackBuffer> buffer) {
  std::lock_guard<std::mutex> lock(retiredMutex_);
  retiredBuffers_.push_back(std::move(buffer));
}

void TrackLoader::purgeRetiredBuffers() noexcept {
  std::lock_guard<std::mutex> lock(retiredMutex_);
  retiredBuffers_.clear();
}

std::size_t TrackLoader::retiredBuffersCount() const noexcept {
  return retiredBuffers_.size();
}

void TrackLoader::workerLoop() {
  while (running_.load(std::memory_order_acquire)) {
    LoadTask task;
    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      queueCv_.wait(lock, [this] { return !running_.load(std::memory_order_relaxed) || !tasks_.empty(); });

      if (!running_.load(std::memory_order_relaxed) && tasks_.empty()) {
        break;
      }

      task = std::move(tasks_.front());
      tasks_.pop_front();
    }

    TrackLoadResult result;
    if (task.player != nullptr) {
      result = loadTrackSync(task.deck, task.path, *task.player);
    } else {
      std::string err;
      auto buf = decodeFile(task.path, &err);
      result.deck = task.deck;
      result.path = task.path;
      result.success = (buf != nullptr);
      result.buffer = buf;
      result.errorMessage = err;
    }

    if (task.callback) {
      task.callback(result);
    }

    // Safely drain older retired buffers on the worker thread
    purgeRetiredBuffers();
  }
}

}  // namespace zyron::audio
