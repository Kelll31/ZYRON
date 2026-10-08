// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>

namespace zyron::analysis {

/// Integrated programme loudness of a whole track, ITU-R BS.1770-4 / EBU R128: K-weighting (a high shelf and a
/// high pass), 400 ms blocks with 75 % overlap, an absolute gate at -70 LUFS and a relative gate 10 LU below the
/// gated mean (SPEC section 31).
///
/// Channel handling: every channel of `channels` is K-weighted and its mean square summed with weight 1.0 (front left,
/// right and centre; surround weights are not needed, the application decodes at most stereo). A mono file must be
/// passed as the same channel twice (as the decoder hands it out): a 1 kHz sine of -20 dBFS peak then reads -20.0 LUFS
/// (-0.691 dB offset cancels the K filter's +0.69 dB at 1 kHz), while the same sine in ONE channel only reads
/// -23.0 LUFS (half the power).
struct LoudnessResult {
  bool valid{false};      // false: shorter than one 400 ms block, or silent (everything below the absolute gate)
  double lufs{0.0};       // integrated loudness; meaningful only when valid
};

/// Biquad coefficients (a0 = 1) of the two K-weighting stages for `sampleRate`, derived from the analogue prototypes
/// (at 48 kHz they equal the tables of BS.1770-4).
struct KWeightingCoefficients {
  std::array<double, 3> shelfB{};
  std::array<double, 2> shelfA{};  // a1, a2
  std::array<double, 3> highPassB{};
  std::array<double, 2> highPassA{};
};
[[nodiscard]] KWeightingCoefficients kWeightingCoefficients(double sampleRate);

[[nodiscard]] LoudnessResult measureIntegratedLoudness(const float* const* channels, int channelCount,
                                                       std::size_t frames, int sampleRate);

}  // namespace zyron::analysis
