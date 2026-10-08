// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Models/ModelManifest.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace zyron::ai {

namespace {

// Standard SHA-256 implementation (FIPS 180-2)
class Sha256Hasher {
 public:
  Sha256Hasher() { reset(); }

  void reset() noexcept {
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    count_ = 0;
    bufferLength_ = 0;
  }

  void update(const std::uint8_t* data, std::size_t length) noexcept {
    count_ += length;
    std::size_t offset = 0;
    while (offset < length) {
      const std::size_t take = std::min(length - offset, 64 - bufferLength_);
      std::memcpy(buffer_.data() + bufferLength_, data + offset, take);
      bufferLength_ += take;
      offset += take;

      if (bufferLength_ == 64) {
        transform(buffer_.data());
        bufferLength_ = 0;
      }
    }
  }

  std::string finalize() {
    std::array<std::uint8_t, 8> lengthBytes{};
    const std::uint64_t bitLength = count_ * 8;
    for (int i = 0; i < 8; ++i) {
      lengthBytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((bitLength >> (56 - 8 * i)) & 0xFF);
    }

    constexpr std::uint8_t pad = 0x80;
    update(&pad, 1);
    while (bufferLength_ != 56) {
      constexpr std::uint8_t zero = 0x00;
      update(&zero, 1);
    }
    update(lengthBytes.data(), 8);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < 8; ++i) {
      ss << std::setw(8) << state_[i];
    }
    return ss.str();
  }

 private:
  static constexpr std::array<std::uint32_t, 64> kK = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

  static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) noexcept {
    return (x >> n) | (x << (32 - n));
  }

  void transform(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
      const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const auto ch = (e & f) ^ ((~e) & g);
      const auto temp1 = h + s1 + ch + kK[i] + w[i];
      const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const auto maj = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = s0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{};
  std::uint64_t count_{0};
  std::size_t bufferLength_{0};
  std::array<std::uint8_t, 64> buffer_{};
};

}  // namespace

ModelManifest::ModelManifest() {
  catalog_ = defaultCatalog();
}

std::vector<core::ModelMetadata> ModelManifest::defaultCatalog() {
  std::vector<core::ModelMetadata> list;

  // 1. HT-Demucs ONNX (Standard 4-stem, MIT)
  {
    core::ModelMetadata m;
    m.id = "htdemucs-onnx";
    m.name = "HT-Demucs 4-Stem";
    m.version = "4.0.0";
    m.task = core::ModelTask::StemSeparation;
    m.filename = "htdemucs.onnx";
    m.sourceUrl = "https://huggingface.co/StemSplitio/htdemucs-onnx";
    m.license = "MIT";
    m.isPermissive = true;
    m.sizeBytes = 331818228ULL;
    m.minVramBytes = 1024ULL * 1024 * 1024;
    m.supportedBackends = "CUDA, CPU, DirectML";
    list.push_back(std::move(m));
  }

  // 2. HT-Demucs Fine-Tuned (High Quality, MIT)
  {
    core::ModelMetadata m;
    m.id = "htdemucs-ft-onnx";
    m.name = "HT-Demucs Fine-Tuned (HQ)";
    m.version = "4.0.0";
    m.task = core::ModelTask::StemSeparation;
    m.filename = "htdemucs_ft.onnx";
    m.sourceUrl = "https://huggingface.co/StemSplitio/htdemucs-ft-onnx";
    m.license = "MIT";
    m.isPermissive = true;
    m.sizeBytes = 1327272912ULL;
    m.minVramBytes = 2048ULL * 1024 * 1024;
    m.supportedBackends = "CUDA, CPU";
    list.push_back(std::move(m));
  }

  // 3. HT-Demucs 6-Stem (Guitar/Piano Extended, MIT)
  {
    core::ModelMetadata m;
    m.id = "htdemucs-6s-onnx";
    m.name = "HT-Demucs 6-Stem Extended";
    m.version = "4.0.0";
    m.task = core::ModelTask::StemSeparation;
    m.filename = "htdemucs_6s.onnx";
    m.sourceUrl = "https://huggingface.co/StemSplitio/htdemucs-6s-onnx";
    m.license = "MIT";
    m.isPermissive = true;
    m.sizeBytes = 270532608ULL;
    m.minVramBytes = 1536ULL * 1024 * 1024;
    m.supportedBackends = "CUDA, CPU";
    list.push_back(std::move(m));
  }

  // 4. Beat This! (BPM / Downbeat detection, MIT)
  {
    core::ModelMetadata m;
    m.id = "beat-this-onnx";
    m.name = "Beat This! BPM & Grid";
    m.version = "1.0.0";
    m.task = core::ModelTask::BeatDetection;
    m.filename = "beat_this.onnx";
    m.sourceUrl = "https://huggingface.co/musetric/beat-this-onnx";
    m.license = "MIT";
    m.isPermissive = true;
    m.sizeBytes = 125829120ULL;
    m.minVramBytes = 512ULL * 1024 * 1024;
    m.supportedBackends = "CUDA, CPU";
    list.push_back(std::move(m));
  }

  // 5. S-KEY (Camelot key detection, MIT)
  {
    core::ModelMetadata m;
    m.id = "skey-onnx";
    m.name = "S-KEY Musical Key";
    m.version = "1.0.0";
    m.task = core::ModelTask::KeyDetection;
    m.filename = "skey.onnx";
    m.sourceUrl = "https://huggingface.co/musetric/skey-onnx";
    m.license = "MIT";
    m.isPermissive = true;
    m.sizeBytes = 356352ULL;
    m.minVramBytes = 128ULL * 1024 * 1024;
    m.supportedBackends = "CUDA, CPU";
    list.push_back(std::move(m));
  }

  // 6. ChordMini (170-class chord detection, MIT)
  {
    core::ModelMetadata m;
    m.id = "chordmini-onnx";
    m.name = "ChordMini Harmony";
    m.version = "1.0.0";
    m.task = core::ModelTask::ChordRecognition;
    m.filename = "chordmini.onnx";
    m.sourceUrl = "https://huggingface.co/musetric/chordmini-onnx";
    m.license = "MIT";
    m.isPermissive = true;
    m.sizeBytes = 17825792ULL;
    m.minVramBytes = 256ULL * 1024 * 1024;
    m.supportedBackends = "CUDA, CPU";
    list.push_back(std::move(m));
  }

  // 7. MERT v2 Full-Song (Optional embedding plug-in, CC BY-NC 4.0, ADR-0013)
  {
    core::ModelMetadata m;
    m.id = "mert-v2-fullsong";
    m.name = "MERT v2 Music Embedding (Non-Commercial)";
    m.version = "2.0.0";
    m.task = core::ModelTask::StructureSegmentation;
    m.filename = "mert_v2.bin";
    m.sourceUrl = "https://huggingface.co/m-a-p/MERT-v2-FullSong";
    m.license = "CC BY-NC 4.0";
    m.isPermissive = false;
    m.sizeBytes = 1200000000ULL;
    m.minVramBytes = 2048ULL * 1024 * 1024;
    m.supportedBackends = "CUDA, CPU";
    list.push_back(std::move(m));
  }

  return list;
}

std::optional<core::ModelMetadata> ModelManifest::findModel(const std::string& modelId) const {
  for (const auto& m : catalog_) {
    if (m.id == modelId) {
      return m;
    }
  }
  return std::nullopt;
}

void ModelManifest::registerModel(core::ModelMetadata metadata) {
  for (auto& m : catalog_) {
    if (m.id == metadata.id) {
      m = std::move(metadata);
      return;
    }
  }
  catalog_.push_back(std::move(metadata));
}

std::string ModelManifest::computeSha256(const std::string& filePath) {
  std::ifstream file(filePath, std::ios::binary);
  if (!file.is_open()) {
    return {};
  }

  Sha256Hasher hasher;
  std::array<std::uint8_t, 65536> buffer{};
  while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || file.gcount() > 0) {
    hasher.update(buffer.data(), static_cast<std::size_t>(file.gcount()));
  }

  return hasher.finalize();
}

bool ModelManifest::verifyFile(const std::string& filePath,
                               std::uint64_t expectedSize,
                               const std::string& expectedSha256) {
  std::error_code ec;
  const auto actualSize = std::filesystem::file_size(filePath, ec);
  if (ec) {
    return false;
  }

  if (expectedSize > 0 && actualSize != expectedSize) {
    return false;
  }

  if (!expectedSha256.empty()) {
    const auto hash = computeSha256(filePath);
    if (hash != expectedSha256) {
      return false;
    }
  }

  return true;
}

}  // namespace zyron::ai
