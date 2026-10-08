// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <string>

#include "Library/Metadata/IMetadataReader.hpp"

namespace zyron::library {

/// High-performance, dependency-free audio metadata reader for WAV, MP3, FLAC, AIFF, and OGG
/// (SPEC sections 27, 29, ADR-0009).
///
/// Features:
///  - RIFF WAV: parses 'fmt ', 'data', and 'LIST INFO' chunks (INAM, IART, IPRD, IGNR, ICRD)
///  - MP3: parses ID3v2.3/v2.4 headers/frames (TIT2, TPE1, TALB, TCON, TYER/TDRC, TLEN)
///         and MPEG audio frame headers (MPEG-1/2, Layer III, Xing/VBRI headers)
///  - FLAC: parses STREAMINFO block (exact sample rate, channels, bit depth, duration)
///          and VORBIS_COMMENT block (TITLE, ARTIST, ALBUM, GENRE, DATE)
///  - AIFF: parses FORM AIFF 'COMM' and metadata chunks
///  - Intelligent fallback to filename parsing ("Artist - Title") if tags are absent.
class BuiltinMetadataReader final : public IMetadataReader {
 public:
  BuiltinMetadataReader() = default;
  ~BuiltinMetadataReader() override = default;

  [[nodiscard]] bool readMetadata(const std::filesystem::path& path,
                                  TrackMetadata& out,
                                  std::string* errorOut = nullptr) override;

 private:
  [[nodiscard]] static bool readWav(const std::filesystem::path& path, TrackMetadata& out);
  [[nodiscard]] static bool readMp3(const std::filesystem::path& path, TrackMetadata& out);
  [[nodiscard]] static bool readFlac(const std::filesystem::path& path, TrackMetadata& out);
  [[nodiscard]] static bool readAiff(const std::filesystem::path& path, TrackMetadata& out);
  [[nodiscard]] static bool readOgg(const std::filesystem::path& path, TrackMetadata& out);

  static void applyFilenameFallback(const std::filesystem::path& path, TrackMetadata& out);
};

}  // namespace zyron::library
