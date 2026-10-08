// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Metadata/BuiltinMetadataReader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <vector>

namespace zyron::library {

namespace {

inline std::uint16_t readLE16(const std::uint8_t* p) noexcept {
  return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

inline std::uint32_t readLE32(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint16_t readBE16(const std::uint8_t* p) noexcept {
  return (static_cast<std::uint16_t>(p[0]) << 8) | static_cast<std::uint16_t>(p[1]);
}

inline std::uint32_t readBE32(const std::uint8_t* p) noexcept {
  return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

inline std::uint32_t readSyncSafe32(const std::uint8_t* p) noexcept {
  return ((static_cast<std::uint32_t>(p[0]) & 0x7FU) << 21) |
         ((static_cast<std::uint32_t>(p[1]) & 0x7FU) << 14) |
         ((static_cast<std::uint32_t>(p[2]) & 0x7FU) << 7) |
         (static_cast<std::uint32_t>(p[3]) & 0x7FU);
}

std::string trim(std::string_view s) {
  while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) || s.front() == '\0')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == '\0')) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

std::string toLower(std::string_view s) {
  std::string result(s);
  for (char& c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

std::string decodeLatin1ToUtf8(const std::uint8_t* data, std::size_t len) {
  std::string out;
  out.reserve(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    const std::uint8_t ch = data[i];
    if (ch < 0x80) {
      out.push_back(static_cast<char>(ch));
    } else {
      out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
      out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
  }
  return trim(out);
}

std::string decodeUtf16ToUtf8(const std::uint8_t* data, std::size_t len, bool bigEndian) {
  std::string out;
  if (len < 2) return {};
  out.reserve(len);

  std::size_t offset = 0;
  if (len >= 2) {
    if (data[0] == 0xFE && data[1] == 0xFF) {
      bigEndian = true;
      offset = 2;
    } else if (data[0] == 0xFF && data[1] == 0xFE) {
      bigEndian = false;
      offset = 2;
    }
  }

  for (std::size_t i = offset; i + 1 < len; i += 2) {
    std::uint16_t cp = bigEndian ? readBE16(data + i) : readLE16(data + i);
    if (cp == 0) break;

    if (cp <= 0x7F) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return trim(out);
}

std::string decodeId3Text(std::uint8_t encoding, const std::uint8_t* data, std::size_t len) {
  if (len == 0) return {};
  switch (encoding) {
    case 0:  // ISO-8859-1
      return decodeLatin1ToUtf8(data, len);
    case 1:  // UTF-16 with BOM
      return decodeUtf16ToUtf8(data, len, false);
    case 2:  // UTF-16BE without BOM
      return decodeUtf16ToUtf8(data, len, true);
    case 3:  // UTF-8
    default:
      return trim(std::string_view(reinterpret_cast<const char*>(data), len));
  }
}

int parseYear(std::string_view s) {
  for (std::size_t i = 0; i + 3 < s.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(s[i])) &&
        std::isdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isdigit(static_cast<unsigned char>(s[i + 2])) &&
        std::isdigit(static_cast<unsigned char>(s[i + 3]))) {
      int y = (s[i] - '0') * 1000 + (s[i + 1] - '0') * 100 +
              (s[i + 2] - '0') * 10 + (s[i + 3] - '0');
      if (y >= 1900 && y <= 2100) return y;
    }
  }
  return 0;
}

void parseVorbisComment(std::string_view comment, TrackMetadata& out) {
  const auto eq = comment.find('=');
  if (eq == std::string_view::npos) return;

  const std::string key = toLower(comment.substr(0, eq));
  const std::string val = trim(comment.substr(eq + 1));
  if (val.empty()) return;

  if (key == "title") {
    out.title = val;
  } else if (key == "artist") {
    out.artist = val;
  } else if (key == "album") {
    out.album = val;
  } else if (key == "genre") {
    out.genre = val;
  } else if (key == "date" || key == "year") {
    out.year = parseYear(val);
  }
}

}  // namespace

bool BuiltinMetadataReader::readWav(const std::filesystem::path& path, TrackMetadata& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;

  std::array<std::uint8_t, 12> header{};
  if (!f.read(reinterpret_cast<char*>(header.data()), 12)) return false;

  if (std::memcmp(header.data(), "RIFF", 4) != 0 ||
      std::memcmp(header.data() + 8, "WAVE", 4) != 0) {
    return false;
  }

  bool foundFmt = false;
  std::uint32_t dataBytes = 0;

  while (f.good()) {
    std::array<std::uint8_t, 8> chunkHeader{};
    if (!f.read(reinterpret_cast<char*>(chunkHeader.data()), 8)) break;

    const auto chunkId = std::string_view(reinterpret_cast<const char*>(chunkHeader.data()), 4);
    const std::uint32_t chunkSize = readLE32(chunkHeader.data() + 4);

    if (chunkId == "fmt ") {
      if (chunkSize < 16) break;
      std::vector<std::uint8_t> fmtData(chunkSize);
      if (!f.read(reinterpret_cast<char*>(fmtData.data()), chunkSize)) break;

      out.channels = readLE16(fmtData.data() + 2);
      out.sampleRate = static_cast<int>(readLE32(fmtData.data() + 4));
      out.bitDepth = readLE16(fmtData.data() + 14);
      foundFmt = true;
    } else if (chunkId == "data") {
      dataBytes = chunkSize;
      f.seekg(chunkSize, std::ios::cur);
    } else if (chunkId == "LIST") {
      if (chunkSize >= 4) {
        std::array<std::uint8_t, 4> listType{};
        if (f.read(reinterpret_cast<char*>(listType.data()), 4)) {
          if (std::memcmp(listType.data(), "INFO", 4) == 0) {
            std::uint32_t bytesRemaining = chunkSize - 4;
            while (bytesRemaining >= 8) {
              std::array<std::uint8_t, 8> sub{};
              if (!f.read(reinterpret_cast<char*>(sub.data()), 8)) break;
              bytesRemaining -= 8;

              const auto subId = std::string_view(reinterpret_cast<const char*>(sub.data()), 4);
              const std::uint32_t subSize = readLE32(sub.data() + 4);
              const std::uint32_t paddedSize = subSize + (subSize & 1);

              if (subSize <= bytesRemaining) {
                std::vector<std::uint8_t> text(subSize);
                if (f.read(reinterpret_cast<char*>(text.data()), subSize)) {
                  std::string val = trim(std::string_view(reinterpret_cast<const char*>(text.data()), subSize));
                  if (subId == "INAM") out.title = val;
                  else if (subId == "IART") out.artist = val;
                  else if (subId == "IPRD") out.album = val;
                  else if (subId == "IGNR") out.genre = val;
                  else if (subId == "ICRD") out.year = parseYear(val);
                }
                if (paddedSize > subSize) {
                  f.seekg(paddedSize - subSize, std::ios::cur);
                }
                bytesRemaining -= paddedSize;
              } else {
                break;
              }
            }
          } else {
            f.seekg(chunkSize - 4, std::ios::cur);
          }
        }
      }
    } else {
      f.seekg(chunkSize + (chunkSize & 1), std::ios::cur);
    }
  }

  if (foundFmt && out.sampleRate > 0 && out.channels > 0 && out.bitDepth > 0 && dataBytes > 0) {
    const double bytesPerSec = static_cast<double>(out.sampleRate * out.channels * (out.bitDepth / 8));
    if (bytesPerSec > 0.0) {
      out.durationSec = static_cast<double>(dataBytes) / bytesPerSec;
    }
    return true;
  }
  return foundFmt;
}

bool BuiltinMetadataReader::readMp3(const std::filesystem::path& path, TrackMetadata& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;

  std::uint32_t id3Size = 0;
  std::array<std::uint8_t, 10> id3Hdr{};
  if (f.read(reinterpret_cast<char*>(id3Hdr.data()), 10)) {
    if (id3Hdr[0] == 'I' && id3Hdr[1] == 'D' && id3Hdr[2] == '3') {
      const int versionMajor = id3Hdr[3];
      id3Size = readSyncSafe32(id3Hdr.data() + 6);
      if (id3Size > 0 && id3Size < 10 * 1024 * 1024) {  // 10 MB limit for tags
        std::vector<std::uint8_t> tagData(id3Size);
        if (f.read(reinterpret_cast<char*>(tagData.data()), id3Size)) {
          std::size_t offset = 0;
          while (offset + 10 <= id3Size) {
            const char* frameIdPtr = reinterpret_cast<const char*>(tagData.data() + offset);
            if (frameIdPtr[0] == 0) break;  // padding
            const std::string frameId(frameIdPtr, 4);

            std::uint32_t frameSize = (versionMajor == 4)
                                          ? readSyncSafe32(tagData.data() + offset + 4)
                                          : readBE32(tagData.data() + offset + 4);
            offset += 10;
            if (frameSize == 0 || offset + frameSize > id3Size) break;

            const std::uint8_t encoding = tagData[offset];
            const std::uint8_t* contentPtr = tagData.data() + offset + 1;
            const std::size_t contentLen = frameSize - 1;

            if (frameId == "TIT2") {
              out.title = decodeId3Text(encoding, contentPtr, contentLen);
            } else if (frameId == "TPE1") {
              out.artist = decodeId3Text(encoding, contentPtr, contentLen);
            } else if (frameId == "TALB") {
              out.album = decodeId3Text(encoding, contentPtr, contentLen);
            } else if (frameId == "TCON") {
              out.genre = decodeId3Text(encoding, contentPtr, contentLen);
            } else if (frameId == "TYER" || frameId == "TDRC") {
              out.year = parseYear(decodeId3Text(encoding, contentPtr, contentLen));
            } else if (frameId == "TLEN") {
              std::string tlenStr = decodeId3Text(encoding, contentPtr, contentLen);
              try {
                const double ms = std::stod(tlenStr);
                if (ms > 0.0) out.durationSec = ms / 1000.0;
              } catch (...) {}
            }
            offset += frameSize;
          }
        }
      }
    } else {
      f.seekg(0, std::ios::beg);
    }
  }

  // Scan for MPEG audio frame header to obtain sample rate, channels, bitrate, and duration
  static constexpr int kSampleRateTable[3][4] = {
      {44100, 48000, 32000, 0},   // MPEG-1
      {22050, 24000, 16000, 0},   // MPEG-2
      {11025, 12000, 8000, 0}     // MPEG-2.5
  };
  static constexpr int kBitrateTableMpeg1L3[16] = {
      0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
  };

  std::vector<std::uint8_t> buffer(32768);
  f.seekg(id3Size > 0 ? (10 + id3Size) : 0, std::ios::beg);
  f.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
  const auto bytesRead = static_cast<std::size_t>(f.gcount());

  bool foundMpeg = false;
  for (std::size_t i = 0; i + 4 <= bytesRead; ++i) {
    if (buffer[i] == 0xFF && (buffer[i + 1] & 0xE0) == 0xE0) {
      const int versionBits = (buffer[i + 1] >> 3) & 3;
      const int layerBits = (buffer[i + 1] >> 1) & 3;
      if (versionBits == 1 || layerBits == 0) continue;  // reserved

      int versionIdx = 0;
      if (versionBits == 3) versionIdx = 0;       // MPEG-1
      else if (versionBits == 2) versionIdx = 1;  // MPEG-2
      else if (versionBits == 0) versionIdx = 2;  // MPEG-2.5

      const int bitrateIdx = (buffer[i + 2] >> 4) & 0x0F;
      const int srateIdx = (buffer[i + 2] >> 2) & 3;
      const int channelMode = (buffer[i + 3] >> 6) & 3;

      if (bitrateIdx == 0 || bitrateIdx == 15 || srateIdx == 3) continue;

      out.sampleRate = kSampleRateTable[versionIdx][srateIdx];
      out.channels = (channelMode == 3) ? 1 : 2;
      out.bitrateKbps = (versionIdx == 0 && layerBits == 1) ? kBitrateTableMpeg1L3[bitrateIdx] : 128;
      foundMpeg = true;

      // Look for Xing/Info VBR header inside the frame
      const std::size_t xingOffset = i + 4 + ((out.channels == 1) ? 17 : 32);
      if (xingOffset + 8 <= bytesRead) {
        if (std::memcmp(buffer.data() + xingOffset, "Xing", 4) == 0 ||
            std::memcmp(buffer.data() + xingOffset, "Info", 4) == 0) {
          const std::uint32_t flags = readBE32(buffer.data() + xingOffset + 4);
          if ((flags & 1) && xingOffset + 12 <= bytesRead) {
            const std::uint32_t frames = readBE32(buffer.data() + xingOffset + 8);
            if (out.sampleRate > 0) {
              out.durationSec = (static_cast<double>(frames) * 1152.0) / out.sampleRate;
            }
          }
        }
      }
      break;
    }
  }

  // Fallback duration calculation for CBR if Xing wasn't found
  if (foundMpeg && out.durationSec <= 0.0 && out.bitrateKbps > 0) {
    const auto fileSize = std::filesystem::file_size(path);
    const auto audioBytes = (fileSize > id3Size + 10) ? (fileSize - id3Size - 10) : fileSize;
    out.durationSec = static_cast<double>(audioBytes * 8) / (out.bitrateKbps * 1000.0);
  }

  return foundMpeg || (id3Size > 0);
}

bool BuiltinMetadataReader::readFlac(const std::filesystem::path& path, TrackMetadata& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;

  std::array<std::uint8_t, 4> magic{};
  if (!f.read(reinterpret_cast<char*>(magic.data()), 4)) return false;
  if (std::memcmp(magic.data(), "fLaC", 4) != 0) return false;

  bool isLast = false;
  while (!isLast && f.good()) {
    std::array<std::uint8_t, 4> hdr{};
    if (!f.read(reinterpret_cast<char*>(hdr.data()), 4)) break;

    isLast = (hdr[0] & 0x80) != 0;
    const int blockType = hdr[0] & 0x7F;
    const std::uint32_t length = (static_cast<std::uint32_t>(hdr[1]) << 16) |
                                 (static_cast<std::uint32_t>(hdr[2]) << 8) |
                                 static_cast<std::uint32_t>(hdr[3]);

    if (blockType == 0 && length >= 34) {  // STREAMINFO
      std::vector<std::uint8_t> block(length);
      if (!f.read(reinterpret_cast<char*>(block.data()), length)) break;

      out.sampleRate = static_cast<int>((block[10] << 12) | (block[11] << 4) | (block[12] >> 4));
      out.channels = ((block[12] >> 1) & 0x07) + 1;
      out.bitDepth = (((block[12] & 0x01) << 4) | (block[13] >> 4)) + 1;

      const std::uint64_t totalSamples = (static_cast<std::uint64_t>(block[13] & 0x0F) << 32) |
                                         (static_cast<std::uint64_t>(block[14]) << 24) |
                                         (static_cast<std::uint64_t>(block[15]) << 16) |
                                         (static_cast<std::uint64_t>(block[16]) << 8) |
                                         static_cast<std::uint64_t>(block[17]);
      if (out.sampleRate > 0) {
        out.durationSec = static_cast<double>(totalSamples) / out.sampleRate;
      }
    } else if (blockType == 4) {  // VORBIS_COMMENT
      std::vector<std::uint8_t> block(length);
      if (!f.read(reinterpret_cast<char*>(block.data()), length)) break;

      std::size_t offset = 0;
      if (offset + 4 <= length) {
        const std::uint32_t vendorLen = readLE32(block.data() + offset);
        offset += 4 + vendorLen;
        if (offset + 4 <= length) {
          const std::uint32_t numComments = readLE32(block.data() + offset);
          offset += 4;
          for (std::uint32_t i = 0; i < numComments && offset + 4 <= length; ++i) {
            const std::uint32_t commentLen = readLE32(block.data() + offset);
            offset += 4;
            if (offset + commentLen <= length) {
              const std::string_view comment(reinterpret_cast<const char*>(block.data() + offset), commentLen);
              parseVorbisComment(comment, out);
              offset += commentLen;
            } else {
              break;
            }
          }
        }
      }
    } else {
      f.seekg(length, std::ios::cur);
    }
  }

  return out.sampleRate > 0;
}

bool BuiltinMetadataReader::readAiff(const std::filesystem::path& path, TrackMetadata& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;

  std::array<std::uint8_t, 12> header{};
  if (!f.read(reinterpret_cast<char*>(header.data()), 12)) return false;

  if (std::memcmp(header.data(), "FORM", 4) != 0 ||
      (std::memcmp(header.data() + 8, "AIFF", 4) != 0 &&
       std::memcmp(header.data() + 8, "AIFC", 4) != 0)) {
    return false;
  }

  bool foundComm = false;
  while (f.good()) {
    std::array<std::uint8_t, 8> chunkHeader{};
    if (!f.read(reinterpret_cast<char*>(chunkHeader.data()), 8)) break;

    const auto chunkId = std::string_view(reinterpret_cast<const char*>(chunkHeader.data()), 4);
    const std::uint32_t chunkSize = readBE32(chunkHeader.data() + 4);

    if (chunkId == "COMM" && chunkSize >= 18) {
      std::vector<std::uint8_t> comm(chunkSize);
      if (!f.read(reinterpret_cast<char*>(comm.data()), chunkSize)) break;

      out.channels = readBE16(comm.data());
      const std::uint32_t numSampleFrames = readBE32(comm.data() + 2);
      out.bitDepth = readBE16(comm.data() + 6);

      // Decode 80-bit IEEE 754 extended float
      const int exponent = ((comm[8] & 0x7F) << 8) | comm[9];
      const std::uint64_t hiMantissa = readBE32(comm.data() + 10);
      const std::uint64_t loMantissa = readBE32(comm.data() + 14);
      const std::uint64_t mantissa = (hiMantissa << 32) | loMantissa;

      if (exponent != 0 || mantissa != 0) {
        const double rate = std::ldexp(static_cast<double>(mantissa) / 9223372036854775808.0, exponent - 16383);
        out.sampleRate = static_cast<int>(std::round(rate));
        if (out.sampleRate > 0) {
          out.durationSec = static_cast<double>(numSampleFrames) / out.sampleRate;
        }
      }
      foundComm = true;
    } else if (chunkId == "NAME") {
      std::vector<std::uint8_t> name(chunkSize);
      if (f.read(reinterpret_cast<char*>(name.data()), chunkSize)) {
        out.title = trim(std::string_view(reinterpret_cast<const char*>(name.data()), chunkSize));
      }
    } else if (chunkId == "AUTH") {
      std::vector<std::uint8_t> auth(chunkSize);
      if (f.read(reinterpret_cast<char*>(auth.data()), chunkSize)) {
        out.artist = trim(std::string_view(reinterpret_cast<const char*>(auth.data()), chunkSize));
      }
    } else {
      f.seekg(chunkSize + (chunkSize & 1), std::ios::cur);
    }
  }

  return foundComm;
}

bool BuiltinMetadataReader::readOgg(const std::filesystem::path& path, TrackMetadata& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;

  std::vector<std::uint8_t> buffer(65536);
  f.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
  const auto bytesRead = static_cast<std::size_t>(f.gcount());
  if (bytesRead < 32 || std::memcmp(buffer.data(), "OggS", 4) != 0) return false;

  // Search for Vorbis identification packet (type 1 + "vorbis")
  for (std::size_t i = 0; i + 30 <= bytesRead; ++i) {
    if (buffer[i] == 0x01 && std::memcmp(buffer.data() + i + 1, "vorbis", 6) == 0) {
      out.channels = buffer[i + 11];
      out.sampleRate = static_cast<int>(readLE32(buffer.data() + i + 12));
      break;
    }
  }

  // Search for Vorbis comment packet (type 3 + "vorbis")
  for (std::size_t i = 0; i + 16 <= bytesRead; ++i) {
    if (buffer[i] == 0x03 && std::memcmp(buffer.data() + i + 1, "vorbis", 6) == 0) {
      std::size_t offset = i + 7;
      if (offset + 4 <= bytesRead) {
        const std::uint32_t vendorLen = readLE32(buffer.data() + offset);
        offset += 4 + vendorLen;
        if (offset + 4 <= bytesRead) {
          const std::uint32_t numComments = readLE32(buffer.data() + offset);
          offset += 4;
          for (std::uint32_t c = 0; c < numComments && offset + 4 <= bytesRead; ++c) {
            const std::uint32_t commentLen = readLE32(buffer.data() + offset);
            offset += 4;
            if (offset + commentLen <= bytesRead) {
              const std::string_view comment(reinterpret_cast<const char*>(buffer.data() + offset), commentLen);
              parseVorbisComment(comment, out);
              offset += commentLen;
            } else {
              break;
            }
          }
        }
      }
      break;
    }
  }

  return out.sampleRate > 0;
}

void BuiltinMetadataReader::applyFilenameFallback(const std::filesystem::path& path, TrackMetadata& out) {
  const std::string stem = path.stem().string();

  if (out.title.empty()) {
    const auto separator = stem.find(" - ");
    if (separator != std::string::npos) {
      if (out.artist.empty()) {
        out.artist = trim(stem.substr(0, separator));
      }
      out.title = trim(stem.substr(separator + 3));
    } else {
      out.title = stem;
    }
  }

  if (out.artist.empty()) {
    const auto parent = path.parent_path().filename().string();
    if (!parent.empty() && parent != "." && parent != "/" && parent != "\\") {
      out.artist = parent;
    } else {
      out.artist = "Unknown Artist";
    }
  }
}

bool BuiltinMetadataReader::readMetadata(const std::filesystem::path& path,
                                         TrackMetadata& out,
                                         std::string* errorOut) {
  out = TrackMetadata{};
  const std::string ext = toLower(path.extension().string());

  bool parsed = false;
  if (ext == ".wav") {
    parsed = readWav(path, out);
  } else if (ext == ".mp3") {
    parsed = readMp3(path, out);
  } else if (ext == ".flac") {
    parsed = readFlac(path, out);
  } else if (ext == ".aiff" || ext == ".aif") {
    parsed = readAiff(path, out);
  } else if (ext == ".ogg") {
    parsed = readOgg(path, out);
  } else {
    // For other formats (e.g. m4a/aac), basic extension recognition
    parsed = false;
  }

  applyFilenameFallback(path, out);

  if (!parsed && out.title.empty()) {
    if (errorOut != nullptr) {
      *errorOut = "Unsupported or unreadable audio format: " + path.string();
    }
    return false;
  }

  return true;
}

}  // namespace zyron::library
