// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace zyron::analysis {

/// Energy of each bar of a track on its phrase grid, so the AI can match the energy of two tracks at a transition
/// (outgoing track's last bars against the incoming track's first bars).
///
/// Values are in 0..1 and relative to the track's own loud level: 1.0 is the 95th percentile of the bar levels,
/// every kBarEnergyRangeDb below that is 0.0 (dB scale, so a breakdown 12 dB under the drop reads 0.6).
/// Bar i covers [originSec + i * barSec, originSec + (i + 1) * barSec).
/// Without a tempo the "bars" are windows of kNominalBarSec, from the audible start.
struct BarProfile {
  static constexpr double kNominalBarSec = 2.0;
  static constexpr double kBarEnergyRangeDb = 30.0;
  static constexpr std::size_t kMaxBars = 2048;

  double originSec{0.0};
  double barSec{0.0};
  std::vector<float> energy;  // full band
  std::vector<float> bass;    // below 150 Hz (the bassline: tells a drop from a breakdown)

  [[nodiscard]] bool empty() const noexcept { return energy.empty() || barSec <= 0.0; }

  /// Compact text form, stored in the library database and the analysis file: "ZB1;origin;bar;count;hex;hex" with
  /// the two series quantised to 8 bit (two hex digits per bar, ~4 characters per bar in total).
  [[nodiscard]] std::string encode() const;
  /// Parses encode()'s output; false (and `out` untouched) for anything malformed.
  [[nodiscard]] static bool decode(std::string_view text, BarProfile& out);
};

struct BarProfileInput {
  const float* mono{nullptr};
  std::size_t frames{0};
  int sampleRate{0};
  double originSec{0.0};
  double barSec{0.0};
  double endSec{0.0};  // bars end at or before this (the audible end of the track)
};

[[nodiscard]] BarProfile computeBarProfile(const BarProfileInput& input);

}  // namespace zyron::analysis
