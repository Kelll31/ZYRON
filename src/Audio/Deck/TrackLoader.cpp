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

void TrackLoader::setFallbackDecoder(DecodeFunction decoder) {
  std::lock_guard<std::mutex> lock(decoderMutex_);
  fallbackDecoder_ = std::move(decoder);
}

std::shared_ptr<TrackBuffer> TrackLoader::decodeFile(const std::filesystem::path& path, std::string* errorOut) {
  // Primary RIFF/WAVE decoder (SPEC section 27)
  std::string wavError;
  auto buffer = WavDecoder::decode(path, &wavError);
  if (buffer != nullptr) {
    return buffer;
  }

  DecodeFunction fallback;
  {
    std::lock_guard<std::mutex> lock(decoderMutex_);
    fallback = fallbackDecoder_;
  }
  if (fallback) {
    std::string fallbackError;
    buffer = fallback(path, &fallbackError);
    if (buffer != nullptr) {
      return buffer;
    }
    if (errorOut != nullptr) {
      *errorOut = fallbackError;
    }
    return nullptr;
  }

  if (errorOut != nullptr) {
    *errorOut = wavError;
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

  // One atomic hand-off: what the deck gave up (old track and its stems) is retired, never freed under the audio thread.
  retireReleased(player.loadTrack(newBuffer));

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

void TrackLoader::retire(std::shared_ptr<const TrackBuffer> buffer) {
  retireBuffer(std::move(buffer));
}

void TrackLoader::unloadTrack(core::DeckId deck, DeckPlayer& player) {
  (void)deck;
  retireReleased(player.unloadTrack());  // also unloads the stems
}

void TrackLoader::retireReleased(DeckPlayer::Released released) {
  for (auto& stem : released.stems) {
    if (stem != nullptr) {
      retireBuffer(std::move(stem));
    }
  }
  if (released.track != nullptr) {
    retireBuffer(std::move(released.track));
  }
}

void TrackLoader::retireBuffer(std::shared_ptr<const TrackBuffer> buffer) {
  std::lock_guard<std::mutex> lock(retiredMutex_);
  retiredBuffers_.push_back(RetiredBuffer{std::chrono::steady_clock::now(), clock_ != nullptr ? clock_->load(std::memory_order_relaxed) : 0,
                                           std::move(buffer)});
}

void TrackLoader::purgeRetiredBuffers() noexcept {
  std::lock_guard<std::mutex> lock(retiredMutex_);
  retiredBuffers_.clear();
}

void TrackLoader::purgeExpiredBuffers() noexcept {
  std::vector<RetiredBuffer> doomed;  // destroyed after the lock is released: freeing a big buffer takes a while
  {
    std::lock_guard<std::mutex> lock(retiredMutex_);
    const auto cutoff = std::chrono::steady_clock::now() - kRetireGrace;
    const std::uint64_t now = clock_ != nullptr ? clock_->load(std::memory_order_relaxed) : 0;
    auto expired = [&](const RetiredBuffer& item) {
      return item.retiredAt <= cutoff && (clock_ == nullptr || now >= item.callbackAt + kRetireCallbacks);
    };
    for (auto& item : retiredBuffers_) {
      if (expired(item)) {
        doomed.push_back(std::move(item));
      }
    }
    std::erase_if(retiredBuffers_, [](const RetiredBuffer& item) { return item.buffer == nullptr; });
  }
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

    // Free older retired buffers on the worker thread, but not one the audio thread may still be reading.
    purgeExpiredBuffers();
  }
}

}  // namespace zyron::audio
