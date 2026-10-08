// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Decoder/WavDecoder.hpp"

#include <cstring>
#include <fstream>
#include <vector>

namespace zyron::audio {

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
constexpr std::uint16_t kWavFormatExtensible = 0xFFFE;

void setError(std::string* errorOut, const std::string& message) {
  if (errorOut != nullptr) {
    *errorOut = message;
  }
}

}  // namespace

std::shared_ptr<TrackBuffer> WavDecoder::decode(const std::filesystem::path& path, std::string* errorOut) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    setError(errorOut, "Cannot open file: " + path.generic_string());
    return nullptr;
  }

  RiffHeader riff{};
  if (!file.read(reinterpret_cast<char*>(&riff), sizeof(riff))) {
    setError(errorOut, "Failed to read RIFF header");
    return nullptr;
  }

  if (std::memcmp(riff.riff, "RIFF", 4) != 0 || std::memcmp(riff.wave, "WAVE", 4) != 0) {
    setError(errorOut, "Not a valid RIFF/WAVE file");
    return nullptr;
  }

  FmtChunk fmt{};
  bool foundFmt = false;
  std::uint32_t dataSize = 0;
  std::streampos dataPos = 0;

  while (file) {
    ChunkHeader chunk{};
    if (!file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk))) {
      break;
    }

    if (std::memcmp(chunk.id, "fmt ", 4) == 0) {
      const auto readBytes = std::min<std::size_t>(chunk.size, sizeof(fmt));
      if (!file.read(reinterpret_cast<char*>(&fmt), static_cast<std::streamsize>(readBytes))) {
        setError(errorOut, "Failed to read fmt chunk");
        return nullptr;
      }
      if (chunk.size > sizeof(fmt)) {
        file.seekg(chunk.size - sizeof(fmt), std::ios::cur);
      }
      foundFmt = true;
    } else if (std::memcmp(chunk.id, "data", 4) == 0) {
      dataSize = chunk.size;
      dataPos = file.tellg();
      break;
    } else {
      // Skip unknown chunk
      file.seekg(chunk.size, std::ios::cur);
    }
  }

  if (!foundFmt || dataSize == 0) {
    setError(errorOut, "Missing fmt or data chunk in WAV file");
    return nullptr;
  }

  if (fmt.numChannels == 0 || fmt.sampleRate == 0 || fmt.blockAlign == 0) {
    setError(errorOut, "Invalid WAV audio format parameters");
    return nullptr;
  }

  const int inChannels = static_cast<int>(fmt.numChannels);
  const int outChannels = (inChannels == 1) ? 2 : inChannels;
  const std::int64_t numFrames = static_cast<std::int64_t>(dataSize / fmt.blockAlign);

  auto buffer = std::make_shared<TrackBuffer>(outChannels, numFrames, static_cast<double>(fmt.sampleRate));

  file.seekg(dataPos);

  const bool isPcm = (fmt.audioFormat == kWavFormatPcm || fmt.audioFormat == kWavFormatExtensible);
  const bool isFloat = (fmt.audioFormat == kWavFormatFloat);

  if (isFloat && fmt.bitsPerSample == 32) {
    std::vector<float> interleaved(static_cast<std::size_t>(inChannels) * 1024);
    std::int64_t framesRemaining = numFrames;
    std::int64_t writeFrame = 0;

    while (framesRemaining > 0 && file) {
      const auto chunkFrames = static_cast<std::size_t>(std::min<std::int64_t>(framesRemaining, 1024));
      const auto bytesToRead = chunkFrames * static_cast<std::size_t>(inChannels) * sizeof(float);
      if (!file.read(reinterpret_cast<char*>(interleaved.data()), static_cast<std::streamsize>(bytesToRead))) {
        break;
      }

      for (std::size_t f = 0; f < chunkFrames; ++f) {
        if (inChannels == 1) {
          const float s = interleaved[f];
          buffer->channelData(0)[writeFrame + static_cast<std::int64_t>(f)] = s;
          buffer->channelData(1)[writeFrame + static_cast<std::int64_t>(f)] = s;
        } else {
          for (int ch = 0; ch < outChannels; ++ch) {
            buffer->channelData(ch)[writeFrame + static_cast<std::int64_t>(f)] =
                interleaved[f * static_cast<std::size_t>(inChannels) + static_cast<std::size_t>(ch)];
          }
        }
      }

      writeFrame += static_cast<std::int64_t>(chunkFrames);
      framesRemaining -= static_cast<std::int64_t>(chunkFrames);
    }
  } else if (isPcm && fmt.bitsPerSample == 16) {
    std::vector<std::int16_t> interleaved(static_cast<std::size_t>(inChannels) * 1024);
    std::int64_t framesRemaining = numFrames;
    std::int64_t writeFrame = 0;
    constexpr float kInvShort = 1.0F / 32768.0F;

    while (framesRemaining > 0 && file) {
      const auto chunkFrames = static_cast<std::size_t>(std::min<std::int64_t>(framesRemaining, 1024));
      const auto bytesToRead = chunkFrames * static_cast<std::size_t>(inChannels) * sizeof(std::int16_t);
      if (!file.read(reinterpret_cast<char*>(interleaved.data()), static_cast<std::streamsize>(bytesToRead))) {
        break;
      }

      for (std::size_t f = 0; f < chunkFrames; ++f) {
        if (inChannels == 1) {
          const float s = static_cast<float>(interleaved[f]) * kInvShort;
          buffer->channelData(0)[writeFrame + static_cast<std::int64_t>(f)] = s;
          buffer->channelData(1)[writeFrame + static_cast<std::int64_t>(f)] = s;
        } else {
          for (int ch = 0; ch < outChannels; ++ch) {
            const float s = static_cast<float>(
                                interleaved[f * static_cast<std::size_t>(inChannels) + static_cast<std::size_t>(ch)]) *
                            kInvShort;
            buffer->channelData(ch)[writeFrame + static_cast<std::int64_t>(f)] = s;
          }
        }
      }

      writeFrame += static_cast<std::int64_t>(chunkFrames);
      framesRemaining -= static_cast<std::int64_t>(chunkFrames);
    }
  } else if (isPcm && fmt.bitsPerSample == 24) {
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(inChannels) * 3 * 1024);
    std::int64_t framesRemaining = numFrames;
    std::int64_t writeFrame = 0;
    constexpr float kInv24 = 1.0F / 8388608.0F;

    while (framesRemaining > 0 && file) {
      const auto chunkFrames = static_cast<std::size_t>(std::min<std::int64_t>(framesRemaining, 1024));
      const auto bytesToRead = chunkFrames * static_cast<std::size_t>(inChannels) * 3;
      if (!file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(bytesToRead))) {
        break;
      }

      for (std::size_t f = 0; f < chunkFrames; ++f) {
        for (int ch = 0; ch < inChannels; ++ch) {
          const std::size_t byteIdx = (f * static_cast<std::size_t>(inChannels) + static_cast<std::size_t>(ch)) * 3;
          std::int32_t val = (static_cast<std::int32_t>(raw[byteIdx + 0])) |
                             (static_cast<std::int32_t>(raw[byteIdx + 1]) << 8) |
                             (static_cast<std::int32_t>(raw[byteIdx + 2]) << 16);
          if ((val & 0x800000) != 0) {
            val |= ~0xFFFFFF;  // Sign extend
          }
          const float s = static_cast<float>(val) * kInv24;
          if (inChannels == 1) {
            buffer->channelData(0)[writeFrame + static_cast<std::int64_t>(f)] = s;
            buffer->channelData(1)[writeFrame + static_cast<std::int64_t>(f)] = s;
          } else {
            buffer->channelData(ch)[writeFrame + static_cast<std::int64_t>(f)] = s;
          }
        }
      }

      writeFrame += static_cast<std::int64_t>(chunkFrames);
      framesRemaining -= static_cast<std::int64_t>(chunkFrames);
    }
  } else {
    setError(errorOut, "Unsupported WAV format: format=" + std::to_string(fmt.audioFormat) +
                           " bits=" + std::to_string(fmt.bitsPerSample));
    return nullptr;
  }

  return buffer;
}

bool WavDecoder::encode(const std::filesystem::path& path, const TrackBuffer& buffer, bool useFloat32,
                        std::string* errorOut) {
  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    setError(errorOut, "Cannot open file for writing: " + path.generic_string());
    return false;
  }

  const int numChannels = buffer.numChannels();
  const std::int64_t numFrames = buffer.numFrames();
  const auto sampleRate = static_cast<std::uint32_t>(buffer.sampleRate());
  const std::uint16_t bitsPerSample = useFloat32 ? 32 : 16;
  const std::uint16_t audioFormat = useFloat32 ? kWavFormatFloat : kWavFormatPcm;
  const std::uint16_t blockAlign = static_cast<std::uint16_t>(numChannels * (bitsPerSample / 8));
  const std::uint32_t byteRate = sampleRate * blockAlign;
  const auto dataSize = static_cast<std::uint32_t>(numFrames * blockAlign);
  const std::uint32_t riffSize = 36 + dataSize;

  RiffHeader riff{};
  std::memcpy(riff.riff, "RIFF", 4);
  riff.fileSize = riffSize;
  std::memcpy(riff.wave, "WAVE", 4);
  file.write(reinterpret_cast<const char*>(&riff), sizeof(riff));

  ChunkHeader fmtHeader{};
  std::memcpy(fmtHeader.id, "fmt ", 4);
  fmtHeader.size = sizeof(FmtChunk);
  file.write(reinterpret_cast<const char*>(&fmtHeader), sizeof(fmtHeader));

  FmtChunk fmt{};
  fmt.audioFormat = audioFormat;
  fmt.numChannels = static_cast<std::uint16_t>(numChannels);
  fmt.sampleRate = sampleRate;
  fmt.byteRate = byteRate;
  fmt.blockAlign = blockAlign;
  fmt.bitsPerSample = bitsPerSample;
  file.write(reinterpret_cast<const char*>(&fmt), sizeof(fmt));

  ChunkHeader dataHeader{};
  std::memcpy(dataHeader.id, "data", 4);
  dataHeader.size = dataSize;
  file.write(reinterpret_cast<const char*>(&dataHeader), sizeof(dataHeader));

  if (useFloat32) {
    std::vector<float> interleaved(static_cast<std::size_t>(numChannels) * 1024);
    for (std::int64_t frame = 0; frame < numFrames; frame += 1024) {
      const auto chunkFrames = static_cast<std::size_t>(std::min<std::int64_t>(1024, numFrames - frame));
      for (std::size_t f = 0; f < chunkFrames; ++f) {
        for (int ch = 0; ch < numChannels; ++ch) {
          interleaved[f * static_cast<std::size_t>(numChannels) + static_cast<std::size_t>(ch)] =
              buffer.sampleAt(ch, frame + static_cast<std::int64_t>(f));
        }
      }
      file.write(reinterpret_cast<const char*>(interleaved.data()),
                 static_cast<std::streamsize>(chunkFrames * static_cast<std::size_t>(numChannels) * sizeof(float)));
    }
  } else {
    std::vector<std::int16_t> interleaved(static_cast<std::size_t>(numChannels) * 1024);
    for (std::int64_t frame = 0; frame < numFrames; frame += 1024) {
      const auto chunkFrames = static_cast<std::size_t>(std::min<std::int64_t>(1024, numFrames - frame));
      for (std::size_t f = 0; f < chunkFrames; ++f) {
        for (int ch = 0; ch < numChannels; ++ch) {
          const float s = buffer.sampleAt(ch, frame + static_cast<std::int64_t>(f));
          const auto clamped = std::clamp(s, -1.0F, 1.0F);
          interleaved[f * static_cast<std::size_t>(numChannels) + static_cast<std::size_t>(ch)] =
              static_cast<std::int16_t>(clamped * 32767.0F);
        }
      }
      file.write(
          reinterpret_cast<const char*>(interleaved.data()),
          static_cast<std::streamsize>(chunkFrames * static_cast<std::size_t>(numChannels) * sizeof(std::int16_t)));
    }
  }

  return file.good();
}

}  // namespace zyron::audio
