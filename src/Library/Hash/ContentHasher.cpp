// SPDX-License-Identifier: AGPL-3.0-only
#include "Library/Hash/ContentHasher.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace zyron::library {

namespace {

// SHA-256 standard round constants (FIPS 180-4)
constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

inline std::uint32_t rotr(std::uint32_t x, unsigned int n) noexcept {
  return (x >> n) | (x << (32U - n));
}

inline std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
  return (x & y) ^ (~x & z);
}

inline std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
  return (x & y) ^ (x & z) ^ (y & z);
}

inline std::uint32_t bigSigma0(std::uint32_t x) noexcept {
  return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline std::uint32_t bigSigma1(std::uint32_t x) noexcept {
  return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline std::uint32_t smallSigma0(std::uint32_t x) noexcept {
  return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline std::uint32_t smallSigma1(std::uint32_t x) noexcept {
  return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

class Sha256Context {
 public:
  Sha256Context() noexcept { reset(); }

  void reset() noexcept {
    state_[0] = 0x6a09e667U;
    state_[1] = 0xbb67ae85U;
    state_[2] = 0x3c6ef372U;
    state_[3] = 0xa54ff53aU;
    state_[4] = 0x510e527fU;
    state_[5] = 0x9b05688cU;
    state_[6] = 0x1f83d9abU;
    state_[7] = 0x5be0cd19U;
    totalBytes_ = 0;
    bufferLen_ = 0;
  }

  void update(const std::uint8_t* data, std::size_t len) noexcept {
    totalBytes_ += len;
    while (len > 0) {
      const std::size_t toCopy = std::min(len, 64U - bufferLen_);
      for (std::size_t i = 0; i < toCopy; ++i) {
        buffer_[bufferLen_ + i] = data[i];
      }
      bufferLen_ += toCopy;
      data += toCopy;
      len -= toCopy;

      if (bufferLen_ == 64U) {
        transformBlock(buffer_.data());
        bufferLen_ = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> finalize() noexcept {
    // Append bit '1' (0x80)
    std::uint8_t padByte = 0x80U;
    update(&padByte, 1);

    // Pad with zeros until length is 56 mod 64
    while (bufferLen_ != 56U) {
      std::uint8_t zero = 0x00U;
      update(&zero, 1);
    }

    // Append 64-bit length in bits (big-endian)
    const std::uint64_t totalBits = (totalBytes_ - 1 - (56U > bufferLen_ ? 56U - bufferLen_ : 0)) * 8U;
    // We already accounted for the padding bytes in totalBytes_, so calculate original bit count
    const std::uint64_t origBits = (totalBytes_ - (bufferLen_ <= 56U ? bufferLen_ : (bufferLen_ + 64U - 56U))) * 8U;
    (void)totalBits;
    (void)origBits;

    // Actually, simply using the saved original length before padding is cleaner.
    // Let's refactor padding directly:
    return finalizeDirect();
  }

  std::array<std::uint8_t, 32> finalizeClean() noexcept {
    const std::uint64_t bitCount = totalBytes_ * 8ULL;

    // 1 byte of 0x80
    buffer_[bufferLen_++] = 0x80U;

    if (bufferLen_ > 56U) {
      while (bufferLen_ < 64U) {
        buffer_[bufferLen_++] = 0x00U;
      }
      transformBlock(buffer_.data());
      bufferLen_ = 0;
    }

    while (bufferLen_ < 56U) {
      buffer_[bufferLen_++] = 0x00U;
    }

    // Append 64-bit bit count in big-endian
    for (int i = 7; i >= 0; --i) {
      buffer_[bufferLen_++] = static_cast<std::uint8_t>((bitCount >> (i * 8)) & 0xFFU);
    }

    transformBlock(buffer_.data());

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < 8; ++i) {
      digest[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFFU);
      digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFFU);
      digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFFU);
      digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFFU);
    }
    return digest;
  }

 private:
  std::array<std::uint8_t, 32> finalizeDirect() noexcept {
    return finalizeClean();
  }

  void transformBlock(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (std::size_t i = 16; i < 64; ++i) {
      w[i] = smallSigma1(w[i - 2]) + w[i - 7] + smallSigma0(w[i - 15]) + w[i - 16];
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
      const std::uint32_t t1 = h + bigSigma1(e) + ch(e, f, g) + kK[i] + w[i];
      const std::uint32_t t2 = bigSigma0(a) + maj(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
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
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t bufferLen_{0};
  std::uint64_t totalBytes_{0};
};

std::string formatHex(const std::array<std::uint8_t, 32>& digest) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (std::uint8_t byte : digest) {
    result.push_back(kHexDigits[(byte >> 4) & 0x0F]);
    result.push_back(kHexDigits[byte & 0x0F]);
  }
  return result;
}

}  // namespace

std::string ContentHasher::hashBytes(const void* data, std::size_t size) {
  Sha256Context ctx;
  if (data != nullptr && size > 0) {
    ctx.update(static_cast<const std::uint8_t*>(data), size);
  }
  return formatHex(ctx.finalizeClean());
}

std::string ContentHasher::hashString(std::string_view str) {
  return hashBytes(str.data(), str.size());
}

std::string ContentHasher::hashFile(const std::filesystem::path& path, std::string* errorOut) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    if (errorOut != nullptr) {
      *errorOut = "Failed to open file for hashing: " + path.string();
    }
    return {};
  }

  Sha256Context ctx;
  constexpr std::size_t kChunkSize = 65536;  // 64 KB
  std::vector<char> buffer(kChunkSize);

  while (file.good()) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize bytesRead = file.gcount();
    if (bytesRead > 0) {
      ctx.update(reinterpret_cast<const std::uint8_t*>(buffer.data()), static_cast<std::size_t>(bytesRead));
    }
  }

  if (file.bad()) {
    if (errorOut != nullptr) {
      *errorOut = "I/O error while reading file: " + path.string();
    }
    return {};
  }

  return formatHex(ctx.finalizeClean());
}

}  // namespace zyron::library
