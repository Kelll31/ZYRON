// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/Audio/WaveformData.hpp"

#include <fstream>

namespace zyron::core {

namespace {

#pragma pack(push, 1)
struct FileHeader {
  std::uint32_t magic;
  std::uint32_t version;
  std::uint32_t sampleRate;
  std::uint16_t channels;
  std::uint16_t samplesPerFrame;
  std::uint32_t detailFramesCount;
  std::uint32_t overviewFramesCount;
  std::uint32_t reserved[2];
};
#pragma pack(pop)

}  // namespace

std::optional<WaveformData> WaveformData::loadFromFile(const std::filesystem::path& path,
                                                       std::string* errorOut) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) {
    if (errorOut) *errorOut = "Failed to open waveform file: " + path.string();
    return std::nullopt;
  }

  FileHeader hdr{};
  if (!f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr))) {
    if (errorOut) *errorOut = "Failed to read waveform file header: " + path.string();
    return std::nullopt;
  }

  if (hdr.magic != kMagic || hdr.version != kCurrentVersion) {
    if (errorOut) *errorOut = "Invalid magic or version in waveform file: " + path.string();
    return std::nullopt;
  }

  WaveformData data;
  data.sampleRate = static_cast<int>(hdr.sampleRate);
  data.channels = static_cast<int>(hdr.channels);
  data.samplesPerFrame = static_cast<int>(hdr.samplesPerFrame);

  if (hdr.detailFramesCount > 0) {
    data.detail.resize(hdr.detailFramesCount);
    if (!f.read(reinterpret_cast<char*>(data.detail.data()),
                static_cast<std::streamsize>(hdr.detailFramesCount * sizeof(WaveformPoint)))) {
      if (errorOut) *errorOut = "Corrupted detail frames in waveform file: " + path.string();
      return std::nullopt;
    }
  }

  if (hdr.overviewFramesCount > 0) {
    data.overview.resize(hdr.overviewFramesCount);
    if (!f.read(reinterpret_cast<char*>(data.overview.data()),
                static_cast<std::streamsize>(hdr.overviewFramesCount * sizeof(WaveformPoint)))) {
      if (errorOut) *errorOut = "Corrupted overview frames in waveform file: " + path.string();
      return std::nullopt;
    }
  }

  return data;
}

bool WaveformData::saveToFile(const std::filesystem::path& path, std::string* errorOut) const {
  std::ofstream f(path, std::ios::binary);
  if (!f.is_open()) {
    if (errorOut) *errorOut = "Failed to create output file: " + path.string();
    return false;
  }

  FileHeader hdr{};
  hdr.magic = kMagic;
  hdr.version = kCurrentVersion;
  hdr.sampleRate = static_cast<std::uint32_t>(sampleRate);
  hdr.channels = static_cast<std::uint16_t>(channels);
  hdr.samplesPerFrame = static_cast<std::uint16_t>(samplesPerFrame);
  hdr.detailFramesCount = static_cast<std::uint32_t>(detail.size());
  hdr.overviewFramesCount = static_cast<std::uint32_t>(overview.size());
  hdr.reserved[0] = 0;
  hdr.reserved[1] = 0;

  f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

  if (!detail.empty()) {
    f.write(reinterpret_cast<const char*>(detail.data()),
            static_cast<std::streamsize>(detail.size() * sizeof(WaveformPoint)));
  }

  if (!overview.empty()) {
    f.write(reinterpret_cast<const char*>(overview.data()),
            static_cast<std::streamsize>(overview.size() * sizeof(WaveformPoint)));
  }

  return f.good();
}

}  // namespace zyron::core
