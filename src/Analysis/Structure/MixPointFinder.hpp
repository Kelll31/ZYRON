// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

namespace zyron::analysis {

/// Where a DJ would mix a track, found from its audio and beat grid (SPEC sections 31, 57):
///  - the audible start and end (silence at either edge is never played);
///  - the first drop: the first sustained bar where the bass comes in at full level, on a phrase boundary;
///  - mix in: the first audible bar (the intro is what the incoming track plays under the outgoing one);
///  - mix out: where the last full-energy section ends (the outro starts), early enough that a whole transition fits
///    before the sound ends.
struct MixPoints {
  double audibleStartSec{0.0};
  double audibleEndSec{0.0};
  double mixInSec{-1.0};
  double mixOutSec{-1.0};
  double dropSec{-1.0};
  std::vector<double> drops;  // every drop, the first one included
};

struct MixPointInput {
  const float* mono{nullptr};
  std::size_t frames{0};
  int sampleRate{0};
  double bpm{0.0};           // 0 when the tempo is unknown: only the audible range and time-based points are found
  double firstBeatSec{0.0};  // any beat of the grid
  double transitionBeats{32.0};
};

[[nodiscard]] MixPoints findMixPoints(const MixPointInput& input);

}  // namespace zyron::analysis
