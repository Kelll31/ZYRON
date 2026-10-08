// SPDX-License-Identifier: AGPL-3.0-only
#include "Recording/WavFileWriter.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace zyron::recording {

namespace {

#pragma pack(push, 1)
struct RiffHeader {
  char riff[4];
  std::uint32_t fileSize;
  char wave[4];
};

struct ChunkHeader {
  char id[4];
  std::uint32_t size;
};

struct FmtChunk {
  std::uint16_t audioFormat;
  std::uint16_t numChannels;
  std::uint32_t sampleRate;
  std::uint32_t byteRate;
  std::uint16_t blockAlign;
  std::uint16_t bitsPerSample;
};
#pragma pack(pop)

constexpr std::uint16_t kWavFormatPcm = 1;
constexpr std::uint16_t kWavFormatFloat = 3;

}  // namespace

WavFileWriter::WavFileWriter() = default;

WavFileWriter::~WavFileWriter() {
  close();
}

bool WavFileWriter::open(const std::filesystem::path& path, int numChannels, double sampleRate, bool useFloat32,
                         std::string* errorOut) {
  close();

  if (numChannels <= 0 || sampleRate <= 0.0) {
    if (errorOut != nullptr) {
      *errorOut = "Invalid channel count or sample rate";
    }
    return false;
  }

  // Ensure parent directory exists
  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
  }

  file_.open(path, std::ios::binary | std::ios::trunc);
  if (!file_.is_open()) {
    if (errorOut != nullptr) {
      *errorOut = "Could not create file: " + path.generic_string();
    }
    return false;
  }

  path_ = path;
  numChannels_ = numChannels;
  sampleRate_ = sampleRate;
  useFloat32_ = useFloat32;
  framesWritten_ = 0;

  const auto sr = static_cast<std::uint32_t>(sampleRate_);
  const std::uint16_t bitsPerSample = useFloat32_ ? 32 : 16;
  const std::uint16_t audioFormat = useFloat32_ ? kWavFormatFloat : kWavFormatPcm;
  const std::uint16_t blockAlign = static_cast<std::uint16_t>(numChannels_ * (bitsPerSample / 8));
  const std::uint32_t byteRate = sr * blockAlign;

  // Placeholder RIFF header
  RiffHeader riff{};
  std::memcpy(riff.riff, "RIFF", 4);
  riff.fileSize = 36;  // updated on close
  std::memcpy(riff.wave, "WAVE", 4);
  file_.write(reinterpret_cast<const char*>(&riff), sizeof(riff));

  // fmt chunk
  ChunkHeader fmtHdr{};
  std::memcpy(fmtHdr.id, "fmt ", 4);
  fmtHdr.size = sizeof(FmtChunk);
  file_.write(reinterpret_cast<const char*>(&fmtHdr), sizeof(fmtHdr));

  FmtChunk fmt{};
  fmt.audioFormat = audioFormat;
  fmt.numChannels = static_cast<std::uint16_t>(numChannels_);
  fmt.sampleRate = sr;
  fmt.byteRate = byteRate;
  fmt.blockAlign = blockAlign;
  fmt.bitsPerSample = bitsPerSample;
  file_.write(reinterpret_cast<const char*>(&fmt), sizeof(fmt));

  // data chunk header with placeholder size
  ChunkHeader dataHdr{};
  std::memcpy(dataHdr.id, "data", 4);
  dataHdr.size = 0;  // updated on close
  file_.write(reinterpret_cast<const char*>(&dataHdr), sizeof(dataHdr));

  return file_.good();
}

bool WavFileWriter::writeFrames(const float* interleavedData, std::size_t numFrames) noexcept {
  if (!file_.is_open() || interleavedData == nullptr || numFrames == 0) {
    return false;
  }

  const std::size_t totalSamples = numFrames * static_cast<std::size_t>(numChannels_);

  if (useFloat32_) {
    file_.write(reinterpret_cast<const char*>(interleavedData),
                static_cast<std::streamsize>(totalSamples * sizeof(float)));
  } else {
    std::vector<std::int16_t> pcm(totalSamples);
    for (std::size_t i = 0; i < totalSamples; ++i) {
      const float s = std::clamp(interleavedData[i], -1.0F, 1.0F);
      pcm[i] = static_cast<std::int16_t>(s * 32767.0F);
    }
    file_.write(reinterpret_cast<const char*>(pcm.data()),
                static_cast<std::streamsize>(totalSamples * sizeof(std::int16_t)));
  }

  if (file_.good()) {
    framesWritten_ += static_cast<std::int64_t>(numFrames);
    return true;
  }
  return false;
}

void WavFileWriter::close() noexcept {
  if (!file_.is_open()) {
    return;
  }

  const std::uint16_t bytesPerSample = useFloat32_ ? 4 : 2;
  const auto dataSize =
      static_cast<std::uint32_t>(framesWritten_ * static_cast<std::int64_t>(numChannels_) * bytesPerSample);
  const std::uint32_t riffSize = 36 + dataSize;

  // Finalize RIFF file size
  file_.seekp(4, std::ios::beg);
  file_.write(reinterpret_cast<const char*>(&riffSize), sizeof(riffSize));

  // Finalize data chunk size
  file_.seekp(40, std::ios::beg);
  file_.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));

  file_.flush();
  file_.close();
}

std::uint64_t WavFileWriter::bytesWritten() const noexcept {
  const std::uint64_t bytesPerSample = useFloat32_ ? 4 : 2;
  return static_cast<std::uint64_t>(framesWritten_) * static_cast<std::uint64_t>(numChannels_) * bytesPerSample;
}

}  // namespace zyron::recording
