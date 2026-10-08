// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zyron::stems {

/// Standard 4-stem channels (SPEC section 32, 43, 61).
enum class StemSlot : std::uint8_t {
  Vocals = 0,
  Drums = 1,
  Bass = 2,
  Other = 3
};

inline constexpr std::size_t kStemCount = 4;

[[nodiscard]] constexpr std::string_view stemSlotName(StemSlot slot) noexcept {
  switch (slot) {
    case StemSlot::Vocals:
      return "Vocals";
    case StemSlot::Drums:
      return "Drums";
    case StemSlot::Bass:
      return "Bass";
    case StemSlot::Other:
      return "Other";
  }
  return "Unknown";
}

[[nodiscard]] constexpr std::size_t stemSlotIndex(StemSlot slot) noexcept {
  return static_cast<std::size_t>(slot);
}

/// Decoded audio buffer for an individual stem track.
struct StemBuffer {
  int channels{2};
  double sampleRate{44100.0};
  std::vector<float> left;
  std::vector<float> right;

  [[nodiscard]] std::size_t numFrames() const noexcept {
    return left.size();
  }

  [[nodiscard]] bool empty() const noexcept {
    return left.empty();
  }

  void resize(std::size_t frames) {
    left.resize(frames, 0.0f);
    right.resize(frames, 0.0f);
  }
};

/// Result of full-track stem separation (SPEC sections 32, 33).
struct StemSeparationResult {
  bool success{false};
  std::string error;
  std::array<StemBuffer, kStemCount> stems;
  double sampleRate{44100.0};
  std::int64_t numFrames{0};
  double processingDurationSec{0.0};

  [[nodiscard]] const StemBuffer& vocals() const noexcept { return stems[stemSlotIndex(StemSlot::Vocals)]; }
  [[nodiscard]] const StemBuffer& drums() const noexcept { return stems[stemSlotIndex(StemSlot::Drums)]; }
  [[nodiscard]] const StemBuffer& bass() const noexcept { return stems[stemSlotIndex(StemSlot::Bass)]; }
  [[nodiscard]] const StemBuffer& other() const noexcept { return stems[stemSlotIndex(StemSlot::Other)]; }

  [[nodiscard]] StemBuffer& vocals() noexcept { return stems[stemSlotIndex(StemSlot::Vocals)]; }
  [[nodiscard]] StemBuffer& drums() noexcept { return stems[stemSlotIndex(StemSlot::Drums)]; }
  [[nodiscard]] StemBuffer& bass() noexcept { return stems[stemSlotIndex(StemSlot::Bass)]; }
  [[nodiscard]] StemBuffer& other() noexcept { return stems[stemSlotIndex(StemSlot::Other)]; }
};

}  // namespace zyron::stems
