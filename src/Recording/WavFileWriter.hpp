// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace zyron::recording {

/// Incremental streaming RIFF/WAVE file writer (SPEC section 46).
/// Writes a standard WAV header, streams audio blocks directly to disk, and finalizes
/// headers on closure without loading the whole recording into memory.
class WavFileWriter {
 public:
  WavFileWriter();
  ~WavFileWriter();

  WavFileWriter(const WavFileWriter&) = delete;
  WavFileWriter& operator=(const WavFileWriter&) = delete;

  /// Opens a new WAV file for streaming. Returns true on success.
  bool open(const std::filesystem::path& path, int numChannels, double sampleRate, bool useFloat32 = true,
            std::string* errorOut = nullptr);

  /// Appends interleaved float audio frames to the file.
  bool writeFrames(const float* interleavedData, std::size_t numFrames) noexcept;

  /// Finalizes header with actual frame count and closes the file.
  void close() noexcept;

  [[nodiscard]] bool isOpen() const noexcept { return file_.is_open(); }
  [[nodiscard]] std::int64_t framesWritten() const noexcept { return framesWritten_; }
  [[nodiscard]] std::uint64_t bytesWritten() const noexcept;
  [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] int numChannels() const noexcept { return numChannels_; }
  [[nodiscard]] const std::filesystem::path& filePath() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
  std::ofstream file_;
  int numChannels_{2};
  double sampleRate_{48000.0};
  bool useFloat32_{true};
  std::int64_t framesWritten_{0};
};

}  // namespace zyron::recording
