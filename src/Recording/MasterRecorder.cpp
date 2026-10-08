// SPDX-License-Identifier: AGPL-3.0-only
#include "Recording/MasterRecorder.hpp"

#include <chrono>

namespace zyron::recording {

MasterRecorder::MasterRecorder() {
  fifoBuffer_.resize(kFifoCapacity, 0.0F);
}

MasterRecorder::~MasterRecorder() {
  stop();
}

bool MasterRecorder::start(const std::filesystem::path& path, double sampleRate, int numChannels, bool useFloat32,
                           std::string* errorOut) {
  stop();

  writeIndex_.store(0, std::memory_order_relaxed);
  readIndex_.store(0, std::memory_order_relaxed);
  overflowDrops_.store(0, std::memory_order_relaxed);

  if (!wavWriter_.open(path, numChannels, sampleRate, useFloat32, errorOut)) {
    return false;
  }

  sampleRate_.store(sampleRate, std::memory_order_relaxed);
  numChannels_.store(numChannels, std::memory_order_relaxed);
  state_.store(RecordingState::Recording, std::memory_order_release);

  threadRunning_.store(true, std::memory_order_release);
  writerThread_ = std::thread(&MasterRecorder::writerLoop, this);
  return true;
}

void MasterRecorder::pause() noexcept {
  if (state_.load(std::memory_order_relaxed) == RecordingState::Recording) {
    state_.store(RecordingState::Paused, std::memory_order_release);
  }
}

void MasterRecorder::resume() noexcept {
  if (state_.load(std::memory_order_relaxed) == RecordingState::Paused) {
    state_.store(RecordingState::Recording, std::memory_order_release);
  }
}

void MasterRecorder::stop() noexcept {
  if (state_.exchange(RecordingState::Stopping, std::memory_order_acq_rel) == RecordingState::Idle) {
    return;
  }

  if (threadRunning_.exchange(false, std::memory_order_acq_rel)) {
    writerCv_.notify_all();
    if (writerThread_.joinable()) {
      writerThread_.join();
    }
  }

  state_.store(RecordingState::Idle, std::memory_order_release);
}

void MasterRecorder::writeSamples(const float* const* channels, int numChannels, int numSamples) noexcept {
  if (state_.load(std::memory_order_relaxed) != RecordingState::Recording) {
    return;
  }
  if (channels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  const int activeCh = std::min(numChannels, numChannels_.load(std::memory_order_relaxed));
  const auto requiredFloats = static_cast<std::size_t>(numSamples * activeCh);

  const std::size_t w = writeIndex_.load(std::memory_order_relaxed);
  const std::size_t r = readIndex_.load(std::memory_order_acquire);

  if ((w - r + requiredFloats) > kFifoCapacity) {
    overflowDrops_.fetch_add(1, std::memory_order_relaxed);
    return;  // Drop frame block to preserve audio thread timing
  }

  for (int i = 0; i < numSamples; ++i) {
    for (int ch = 0; ch < activeCh; ++ch) {
      const float s = (channels[ch] != nullptr) ? channels[ch][i] : 0.0F;
      const std::size_t idx = (w + static_cast<std::size_t>(i * activeCh + ch)) & (kFifoCapacity - 1);
      fifoBuffer_[idx] = s;
    }
  }

  writeIndex_.store(w + requiredFloats, std::memory_order_release);
  writerCv_.notify_one();
}

void MasterRecorder::drainFifo() {
  const int channels = numChannels_.load(std::memory_order_relaxed);
  if (channels <= 0) {
    return;
  }

  const std::size_t w = writeIndex_.load(std::memory_order_acquire);
  std::size_t r = readIndex_.load(std::memory_order_relaxed);

  constexpr std::size_t kChunkFloats = 4096;
  std::vector<float> chunk(kChunkFloats);

  while (r < w) {
    const std::size_t available = w - r;
    const std::size_t toRead = std::min(available, kChunkFloats);
    const std::size_t frames = toRead / static_cast<std::size_t>(channels);

    if (frames == 0) {
      break;
    }

    const std::size_t floatsToCopy = frames * static_cast<std::size_t>(channels);
    for (std::size_t i = 0; i < floatsToCopy; ++i) {
      chunk[i] = fifoBuffer_[(r + i) & (kFifoCapacity - 1)];
    }

    wavWriter_.writeFrames(chunk.data(), frames);
    r += floatsToCopy;
    readIndex_.store(r, std::memory_order_release);
  }
}

void MasterRecorder::writerLoop() {
  while (threadRunning_.load(std::memory_order_acquire)) {
    {
      std::unique_lock<std::mutex> lock(writerMutex_);
      writerCv_.wait_for(lock, std::chrono::milliseconds(25), [this] {
        return !threadRunning_.load(std::memory_order_relaxed) ||
               (writeIndex_.load(std::memory_order_relaxed) != readIndex_.load(std::memory_order_relaxed));
      });
    }
    drainFifo();
  }

  // Final flush of any pending samples
  drainFifo();
  wavWriter_.close();
}

bool MasterRecorder::isRecording() const noexcept {
  return state_.load(std::memory_order_relaxed) == RecordingState::Recording;
}

bool MasterRecorder::isPaused() const noexcept {
  return state_.load(std::memory_order_relaxed) == RecordingState::Paused;
}

std::int64_t MasterRecorder::recordedFrames() const noexcept {
  return wavWriter_.framesWritten();
}

double MasterRecorder::recordedDurationSec() const noexcept {
  const double sr = sampleRate_.load(std::memory_order_relaxed);
  return (sr > 0.0) ? (static_cast<double>(recordedFrames()) / sr) : 0.0;
}

std::uint64_t MasterRecorder::overflowDropsCount() const noexcept {
  return overflowDrops_.load(std::memory_order_relaxed);
}

std::filesystem::path MasterRecorder::currentFilePath() const {
  return wavWriter_.filePath();
}

}  // namespace zyron::recording
