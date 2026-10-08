// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

namespace zyron::analysis {

/// A stretch of a track with a singing or speaking voice, in seconds.
struct VocalRegion {
  double startSec{0.0};
  double endSec{0.0};
};

/// The bar grid the decision is made on (a window of `barSec` from `originSec`, `bars` of them).
struct VocalGrid {
  double originSec{0.0};
  double barSec{0.0};
  int bars{0};
};

/// Tunables, in one place.
inline constexpr double kMinCepstralProminence = 4.5;
inline constexpr double kMinVoicedShare = 0.45;
inline constexpr double kMinCentroidVariation = 0.12;
inline constexpr int kMinRegionBars = 2;
inline constexpr int kBridgeGapBars = 1;

/// Vocal regions from the finished mix, without stems (SPEC section 31). A conservative DSP heuristic:
///  1. The mix is reduced to ~11 kHz and cut into 1024-sample frames (50 % overlap).
///  2. A frame is "voiced" when its log spectrum between 100 Hz and 4 kHz shows a harmonic comb whose spacing is a
///     human fundamental (80-400 Hz): the cepstral peak of that range stands out kMinCepstralProminence times above
///     the cepstrum's average. Frames whose 300-3400 Hz energy is far below the track's typical level are ignored.
///  3. A bar is "vocal" when at least kMinVoicedShare of its frames are voiced AND the spectral centroid of the voice
///     band moves inside the bar (coefficient of variation >= kMinCentroidVariation): a held synthesiser note or pad
///     has a harmonic comb too, but a still spectrum; a voice changes vowel and pitch all the time.
///  4. Bar flags become regions: gaps of one bar are bridged, regions shorter than kMinRegionBars are dropped.
/// Known failure modes: a monophonic lead with a moving filter or vibrato reads as a voice; a sustained single vowel,
/// a whisper, a heavily vocoded or buried voice, and rap over a dense midrange are missed (the heuristic prefers
/// missing a vocal to inventing one).
[[nodiscard]] std::vector<VocalRegion> detectVocals(const float* mono, std::size_t frames, int sampleRate,
                                                    const VocalGrid& grid);

/// Vocal regions from a separated vocal stem: a bar is vocal when the stem's RMS in it is above an absolute floor
/// and above a share of the stem's own loud level (the separator leaks a little of the other instruments).
[[nodiscard]] std::vector<VocalRegion> vocalsFromStem(const float* vocalMono, std::size_t frames, int sampleRate,
                                                      const VocalGrid& grid);

}  // namespace zyron::analysis
