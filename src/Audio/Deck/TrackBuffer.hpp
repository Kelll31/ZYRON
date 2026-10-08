// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace zyron::audio {

/// Preallocated, immutable-after-creation multi-channel audio buffer holding a decoded track in memory.
/// Real-time safe: reading from TrackBuffer does not allocate, lock or throw (SPEC sections 10, 12, 46).
class TrackBuffer {
 public:
  TrackBuffer(int numChannels, std::int64_t numFrames, double sampleRate)
      : sampleRate_(sampleRate > 0.0 ? sampleRate : 48000.0),
        numFrames_(std::max<std::int64_t>(0, numFrames)),
        channels_(static_cast<std::size_t>(std::max(0, numChannels)),
                  std::vector<float>(static_cast<std::size_t>(std::max<std::int64_t>(0, numFrames)), 0.0F)) {}

  [[nodiscard]] int numChannels() const noexcept { return static_cast<int>(channels_.size()); }

  [[nodiscard]] std::int64_t numFrames() const noexcept { return numFrames_; }

  [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

  [[nodiscard]] double durationSec() const noexcept {
    return sampleRate_ > 0.0 ? static_cast<double>(numFrames_) / sampleRate_ : 0.0;
  }

  /// Mutable channel pointer used by file decoders / loaders on non-realtime threads.
  [[nodiscard]] float* channelData(int channel) noexcept {
    if (channel < 0 || channel >= numChannels()) {
      return nullptr;
    }
    return channels_[static_cast<std::size_t>(channel)].data();
  }

  /// Const channel pointer used by audio playback thread.
  [[nodiscard]] const float* channelData(int channel) const noexcept {
    if (channel < 0 || channel >= numChannels()) {
      return nullptr;
    }
    return channels_[static_cast<std::size_t>(channel)].data();
  }

  /// Sample access with bounds checking; returns 0.0F outside the buffer.
  [[nodiscard]] float sampleAt(int channel, std::int64_t frame) const noexcept {
    if (channel < 0 || channel >= numChannels() || frame < 0 || frame >= numFrames_) {
      return 0.0F;
    }
    return channels_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(frame)];
  }

 private:
  double sampleRate_{48000.0};
  std::int64_t numFrames_{0};
  std::vector<std::vector<float>> channels_;
};

}  // namespace zyron::audio
