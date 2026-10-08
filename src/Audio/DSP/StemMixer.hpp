// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

#include "Audio/DSP/DjFilter.hpp"
#include "Audio/DSP/ThreeBandEq.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// 4-channel Stem Mixer (SPEC section 43, ROADMAP P5-06).
///
/// Provides per-stem controls for Vocals, Drums, Bass, Other:
///  - Volume (0.0 .. 1.0 linear) with 5 ms click-free parameter smoothing
///  - Mute (true/false) with smooth fade
///  - Solo (true/false): isolating one or multiple stems
///  - Headphone Cue (PFL routing per stem)
///  - 3-band Isolator EQ and bipolar DJ filter per stem
///  - Peak level metering (left/right) per stem
///  - Completely allocation-free and realtime safe.
class StemMixer {
 public:
  StemMixer();
  ~StemMixer() = default;

  StemMixer(const StemMixer&) = delete;
  StemMixer& operator=(const StemMixer&) = delete;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  // Per-stem level and routing controls
  void setVolume(core::StemKind stem, float volumeLinear) noexcept;
  [[nodiscard]] float volume(core::StemKind stem) const noexcept;

  void setMute(core::StemKind stem, bool muted) noexcept;
  [[nodiscard]] bool isMuted(core::StemKind stem) const noexcept;

  void setSolo(core::StemKind stem, bool solo) noexcept;
  [[nodiscard]] bool isSolo(core::StemKind stem) const noexcept;

  void setCue(core::StemKind stem, bool cue) noexcept;
  [[nodiscard]] bool isCue(core::StemKind stem) const noexcept;

  // Per-stem tone controls
  void setEq(core::StemKind stem, core::EqBand band, float gainDb) noexcept;
  void setFilter(core::StemKind stem, float bipolarKnob) noexcept;

  // Per-stem telemetry (peak meters)
  [[nodiscard]] float peakLeft(core::StemKind stem) const noexcept;
  [[nodiscard]] float peakRight(core::StemKind stem) const noexcept;

  /// Mixes the 4 planar input stems into stereo master output.
  /// Zero allocations, lock-free, realtime safe.
  void process(const float* const* stemLefts, const float* const* stemRights, float* masterLeft, float* masterRight,
               int numSamples) noexcept;

  /// Mixes the cued stems into stereo headphone cue output.
  /// Zero allocations, lock-free, realtime safe.
  void processCue(const float* const* stemLefts, const float* const* stemRights, float* cueLeft, float* cueRight,
                  int numSamples) noexcept;

  /// Processes the 4 stems separately (applies volume, mute, solo, EQ, filter) into separate output buffers
  /// for split-track mixer routing (SPEC section 44). Zero allocations, realtime safe.
  void processStems(const float* const* stemLefts, const float* const* stemRights, float* const* outLefts,
                    float* const* outRights, int numSamples) noexcept;

 private:
  struct StemChannel {
    std::atomic<float> targetVolume{1.0F};
    std::atomic<bool> muted{false};
    std::atomic<bool> solo{false};
    std::atomic<bool> cue{false};

    float currentGain{1.0F};

    ThreeBandEq eq;
    DjFilter filter;
    bool hasActiveEqOrFilter{false};

    std::atomic<float> peakLeft{0.0F};
    std::atomic<float> peakRight{0.0F};
  };

  double sampleRate_{48000.0};
  float rampCoeff_{0.005F};

  std::array<StemChannel, core::kStemKindCount> stems_{};

  // Preallocated scratch buffers for realtime in-place processing
  std::vector<float> scratchLeft_{};
  std::vector<float> scratchRight_{};
};

}  // namespace zyron::audio
