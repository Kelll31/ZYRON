// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace zyron::analysis {

struct BeatgridData {
  double bpm{120.0};
  std::int64_t firstBeatFrame{0};
  int sampleRate{44100};
  int downbeatOffset{0};       // 0..3: which beat in the cycle is bar start
  std::string source{"auto"};  // "auto" or "user"

  [[nodiscard]] double samplesPerBeat() const noexcept {
    return (bpm > 0.0 && sampleRate > 0) ? (static_cast<double>(sampleRate) * 60.0 / bpm) : 0.0;
  }

  [[nodiscard]] std::string toJson() const;
  static bool fromJson(std::string_view json, BeatgridData& out);
};

}  // namespace zyron::analysis
