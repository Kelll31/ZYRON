// SPDX-License-Identifier: AGPL-3.0-only
#include "Stems/Cache/StemCache.hpp"

#include <chrono>
#include <fstream>
#include <system_error>

namespace zyron::stems {

namespace {

void writeString(std::ostream& out, std::string_view str) {
  const auto len = static_cast<std::uint32_t>(str.size());
  out.write(reinterpret_cast<const char*>(&len), sizeof(len));
  if (len > 0) {
    out.write(str.data(), static_cast<std::streamsize>(len));
  }
}

bool readString(std::istream& in, std::string& out) {
  std::uint32_t len = 0;
  if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) {
    return false;
  }
  if (len > 1024 * 1024) {  // Sanity upper bound
    return false;
  }
  out.resize(len);
  if (len > 0) {
    if (!in.read(out.data(), static_cast<std::streamsize>(len))) {
      return false;
    }
  }
  return true;
}

std::string sanitizeKeyPart(std::string_view key) {
  std::string s;
  s.reserve(key.size());
  for (char c : key) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
      s.push_back(c);
    } else {
      s.push_back('_');
    }
  }
  return s;
}

}  // namespace

StemCache::StemCache(std::filesystem::path cacheDirectory)
    : cacheDir_(std::move(cacheDirectory)) {
  std::error_code ec;
  std::filesystem::create_directories(cacheDir_, ec);
}

std::filesystem::path StemCache::entryPath(std::string_view contentHash,
                                           std::string_view modelName,
                                           std::string_view modelVersion) const {
  const std::string filename = sanitizeKeyPart(contentHash) + "_" +
                               sanitizeKeyPart(modelName) + "_" +
                               sanitizeKeyPart(modelVersion) + ".zyst";
  return cacheDir_ / filename;
}

bool StemCache::hasStems(std::string_view contentHash,
                         std::string_view modelName,
                         std::string_view modelVersion) const {
  const auto p = entryPath(contentHash, modelName, modelVersion);
  std::error_code ec;
  return std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec) &&
         std::filesystem::file_size(p, ec) > sizeof(std::uint32_t) * 4;
}

std::optional<StemSeparationResult> StemCache::loadStems(std::string_view contentHash,
                                                         std::string_view modelName,
                                                         std::string_view modelVersion) const {
  const auto p = entryPath(contentHash, modelName, modelVersion);
  std::ifstream in(p, std::ios::binary);
  if (!in.is_open()) {
    return std::nullopt;
  }

  std::uint32_t magic = 0;
  std::uint32_t version = 0;
  if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic)) || magic != kMagic) {
    return std::nullopt;
  }
  if (!in.read(reinterpret_cast<char*>(&version), sizeof(version)) || version != kFormatVersion) {
    return std::nullopt;
  }

  std::string storedHash;
  std::string storedModel;
  std::string storedVer;
  if (!readString(in, storedHash) || storedHash != contentHash) return std::nullopt;
  if (!readString(in, storedModel) || storedModel != modelName) return std::nullopt;
  if (!readString(in, storedVer) || storedVer != modelVersion) return std::nullopt;

  double sampleRate = 0.0;
  std::uint64_t numFrames = 0;
  std::uint32_t channels = 0;
  std::uint32_t stemCount = 0;

  if (!in.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate)) || sampleRate <= 0.0) return std::nullopt;
  if (!in.read(reinterpret_cast<char*>(&numFrames), sizeof(numFrames)) || numFrames == 0) return std::nullopt;
  if (!in.read(reinterpret_cast<char*>(&channels), sizeof(channels)) || channels == 0) return std::nullopt;
  if (!in.read(reinterpret_cast<char*>(&stemCount), sizeof(stemCount)) || stemCount != kStemCount) return std::nullopt;

  StemSeparationResult result;
  result.sampleRate = sampleRate;
  result.numFrames = static_cast<std::int64_t>(numFrames);
  result.success = true;

  const auto frames = static_cast<std::size_t>(numFrames);
  const auto byteSize = static_cast<std::streamsize>(frames * sizeof(float));

  for (std::size_t s = 0; s < kStemCount; ++s) {
    auto& buf = result.stems[s];
    buf.sampleRate = sampleRate;
    buf.channels = static_cast<int>(channels);
    buf.resize(frames);

    if (!in.read(reinterpret_cast<char*>(buf.left.data()), byteSize)) return std::nullopt;
    if (channels > 1) {
      if (!in.read(reinterpret_cast<char*>(buf.right.data()), byteSize)) return std::nullopt;
    } else {
      buf.right = buf.left;
    }
  }

  return result;
}

bool StemCache::storeStems(std::string_view contentHash,
                           std::string_view modelName,
                           std::string_view modelVersion,
                           const StemSeparationResult& result) {
  if (!result.success || result.numFrames <= 0 || result.sampleRate <= 0.0) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(cacheDir_, ec);

  const auto targetPath = entryPath(contentHash, modelName, modelVersion);
  const auto tmpPath = cacheDir_ / (targetPath.filename().string() + ".tmp");

  {
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }

    const std::uint32_t magic = kMagic;
    const std::uint32_t version = kFormatVersion;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));

    writeString(out, contentHash);
    writeString(out, modelName);
    writeString(out, modelVersion);

    const double sampleRate = result.sampleRate;
    const auto numFrames = static_cast<std::uint64_t>(result.numFrames);
    const std::uint32_t channels = 2;
    const std::uint32_t stemCount = static_cast<std::uint32_t>(kStemCount);

    out.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
    out.write(reinterpret_cast<const char*>(&numFrames), sizeof(numFrames));
    out.write(reinterpret_cast<const char*>(&channels), sizeof(channels));
    out.write(reinterpret_cast<const char*>(&stemCount), sizeof(stemCount));

    const auto frames = static_cast<std::size_t>(numFrames);
    const auto byteSize = static_cast<std::streamsize>(frames * sizeof(float));

    for (std::size_t s = 0; s < kStemCount; ++s) {
      const auto& buf = result.stems[s];
      if (buf.left.size() != frames) {
        return false;
      }
      out.write(reinterpret_cast<const char*>(buf.left.data()), byteSize);
      if (buf.right.size() == frames) {
        out.write(reinterpret_cast<const char*>(buf.right.data()), byteSize);
      } else {
        out.write(reinterpret_cast<const char*>(buf.left.data()), byteSize);
      }
    }

    if (!out.good()) {
      return false;
    }
  }

  // Atomic replace
  std::filesystem::rename(tmpPath, targetPath, ec);
  if (ec) {
    std::filesystem::remove(tmpPath, ec);
    return false;
  }
  return true;
}

bool StemCache::invalidate(std::string_view contentHash) {
  const std::string prefix = sanitizeKeyPart(contentHash) + "_";
  std::error_code ec;
  bool removedAny = false;

  for (const auto& entry : std::filesystem::directory_iterator(cacheDir_, ec)) {
    if (entry.is_regular_file(ec)) {
      const std::string fn = entry.path().filename().string();
      if (fn.rfind(prefix, 0) == 0 && fn.ends_with(".zyst")) {
        if (std::filesystem::remove(entry.path(), ec)) {
          removedAny = true;
        }
      }
    }
  }
  return removedAny;
}

std::uint64_t StemCache::totalCacheSizeBytes() const {
  std::error_code ec;
  std::uint64_t total = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(cacheDir_, ec)) {
    if (entry.is_regular_file(ec)) {
      total += entry.file_size(ec);
    }
  }
  return total;
}

}  // namespace zyron::stems
