// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Core/Audio/WaveformData.hpp"

namespace zyron::analysis {

/// Single summary frame of audio waveform peaks with 3-band frequency energy (SPEC section 24, 31).
struct WaveformFrame {
  float minLeft{0.0f};
  float maxLeft{0.0f};
  float minRight{0.0f};
  float maxRight{0.0f};
  float lowEnergy{0.0f};   // 0.0 .. 1.0 (bass: < 250 Hz)
  float midEnergy{0.0f};   // 0.0 .. 1.0 (mid: 250 Hz .. 2500 Hz)
  float highEnergy{0.0f};  // 0.0 .. 1.0 (treble: > 2500 Hz)
};

/// Multi-resolution waveform peaks representation with high-performance binary serialization.
/// Stored in cache files (.zywv) referenced by content hash (ARCHITECTURE section 9).
class WaveformPeaks {
 public:
  static constexpr std::uint32_t kMagic = 0x5657595AU;  // 'ZYWV' in little-endian
  static constexpr std::uint32_t kCurrentVersion = 1;
  static constexpr int kDefaultDetailBlockSize = 256;   // samples per detail frame
  static constexpr int kOverviewDownsampleFactor = 8;   // downsampling factor for overview

  WaveformPeaks() = default;

  [[nodiscard]] int sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] int channels() const noexcept { return channels_; }
  [[nodiscard]] int samplesPerFrame() const noexcept { return samplesPerFrame_; }

  [[nodiscard]] const std::vector<WaveformFrame>& detailFrames() const noexcept { return detailFrames_; }
  [[nodiscard]] const std::vector<WaveformFrame>& overviewFrames() const noexcept { return overviewFrames_; }

  void setAttributes(int sampleRate, int channels, int samplesPerFrame) noexcept {
    sampleRate_ = sampleRate;
    channels_ = channels;
    samplesPerFrame_ = samplesPerFrame;
  }

  void addDetailFrame(const WaveformFrame& frame) { detailFrames_.push_back(frame); }
  void setDetailFrames(std::vector<WaveformFrame> frames) { detailFrames_ = std::move(frames); }
  void setOverviewFrames(std::vector<WaveformFrame> frames) { overviewFrames_ = std::move(frames); }

  /// Generates the downsampled overviewFrames from detailFrames.
  void buildOverview(int factor = kOverviewDownsampleFactor);

  /// Saves peaks data to a binary .zywv file.
  [[nodiscard]] bool saveToFile(const std::filesystem::path& path,
                                std::string* errorOut = nullptr) const;

  /// Loads peaks data from a binary .zywv file.
  [[nodiscard]] static std::optional<WaveformPeaks> loadFromFile(
      const std::filesystem::path& path, std::string* errorOut = nullptr);

  /// Converts to core::WaveformData for UI rendering and telemetry.
  [[nodiscard]] core::WaveformData toWaveformData() const;

 private:
  int sampleRate_{44100};
  int channels_{2};
  int samplesPerFrame_{kDefaultDetailBlockSize};
  std::vector<WaveformFrame> detailFrames_;
  std::vector<WaveformFrame> overviewFrames_;
};

/// High-speed 3-band waveform peak generator.
class WaveformGenerator {
 public:
  /// Generates waveform peaks from multichannel audio samples.
  [[nodiscard]] static WaveformPeaks generate(const float* const* channelData,
                                              int numChannels,
                                              std::size_t numSamples,
                                              int sampleRate,
                                              int samplesPerFrame = WaveformPeaks::kDefaultDetailBlockSize);

  /// Generates waveform peaks from interleaved float audio samples.
  [[nodiscard]] static WaveformPeaks generateInterleaved(const float* interleavedData,
                                                         int numChannels,
                                                         std::size_t numFrames,
                                                         int sampleRate,
                                                         int samplesPerFrame = WaveformPeaks::kDefaultDetailBlockSize);
};

}  // namespace zyron::analysis
