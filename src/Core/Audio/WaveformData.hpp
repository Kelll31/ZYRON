// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace zyron::core {

/// Single summary frame of audio waveform peaks with 3-band frequency energy (SPEC sections 24, 31).
struct WaveformPoint {
  float minLeft{0.0f};
  float maxLeft{0.0f};
  float minRight{0.0f};
  float maxRight{0.0f};
  float lowEnergy{0.0f};   // 0.0 .. 1.0 (bass: < 250 Hz)
  float midEnergy{0.0f};   // 0.0 .. 1.0 (mid: 250 Hz .. 2500 Hz)
  float highEnergy{0.0f};  // 0.0 .. 1.0 (treble: > 2500 Hz)
};

/// Multi-resolution waveform peak data used across analysis, persistence, and UI rendering (SPEC section 24).
struct WaveformData {
  static constexpr std::uint32_t kMagic = 0x5657595AU;  // 'ZYWV' in little-endian
  static constexpr std::uint32_t kCurrentVersion = 1;

  int sampleRate{44100};
  int channels{2};
  int samplesPerFrame{256};
  std::vector<WaveformPoint> detail;
  std::vector<WaveformPoint> overview;

  [[nodiscard]] bool empty() const noexcept {
    return detail.empty() && overview.empty();
  }

  [[nodiscard]] double durationSec() const noexcept {
    if (sampleRate <= 0 || samplesPerFrame <= 0) return 0.0;
    if (!detail.empty()) {
      return (static_cast<double>(detail.size()) * samplesPerFrame) / static_cast<double>(sampleRate);
    }
    return (static_cast<double>(overview.size()) * samplesPerFrame * 8) / static_cast<double>(sampleRate);
  }

  /// Loads waveform peaks data from binary .zywv file.
  [[nodiscard]] static std::optional<WaveformData> loadFromFile(
      const std::filesystem::path& path, std::string* errorOut = nullptr);

  /// Saves waveform peaks data to binary .zywv file.
  [[nodiscard]] bool saveToFile(
      const std::filesystem::path& path, std::string* errorOut = nullptr) const;
};

}  // namespace zyron::core
