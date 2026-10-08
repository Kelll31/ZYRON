// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Audio/DSP/StateVariableFilter.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Synthesized DJ performance hits (TriggerFxHit): air horn, dub siren, riser, downlifter, impact, laser.
///
/// Nothing is sampled: every hit is computed on the audio thread from oscillators, noise and envelopes, so there are no
/// sample files and no licences to carry. Hits are summed into the master bus in front of the glue compressor and the
/// limiter (Mixer). A hit at level 1 peaks at about -6 dBFS (each type has its own calibration constant), so one hit
/// over a full-scale track leaves the limiter a few dB of work, not a slam.
///
/// Timing: when `beatSeconds` > 0 the lengths scale to the beat (air horn 3 beats with the stabs on the eighth notes:
/// three short ones and one long; siren 8 beats, one sweep per beat; riser and downlifter 4 beats; impact 6 beats;
/// laser 1 beat). Free (0) assumes a 0.5 s beat. Lengths are capped at 12 s. Every envelope starts and ends at exactly
/// zero and is shaped (raised cosine) so there is no step at either end.
///
/// Voices: kMaxVoices audible at once. A fifth hit steals the oldest voice, which fades out over 5 ms (it keeps its
/// slot until then) so the steal does not click. Fixed arrays only: no allocation, no locks. Not thread-safe: call
/// trigger() and process() from the audio thread (messages arrive through the RtMessage queue).
class FxHitPlayer {
 public:
  static constexpr int kMaxVoices = 4;
  static constexpr double kDefaultBeatSeconds = 0.5;
  static constexpr double kMaxLengthSeconds = 12.0;
  static constexpr double kStealFadeSeconds = 0.005;

  FxHitPlayer() = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /// Starts a hit. `level` is clamped to 0..1 (0 starts nothing); `beatSeconds` <= 0 or non-finite means free.
  void trigger(core::FxHitType type, float level, double beatSeconds) noexcept;

  /// Adds all active hits to `left`/`right`.
  // RT
  void process(float* left, float* right, int numSamples) noexcept;

  /// Hits still sounding (voices being stolen do not count).
  [[nodiscard]] int activeVoices() const noexcept;

  /// Length of a hit in samples for these settings (what process() renders before the voice frees itself).
  [[nodiscard]] static std::int64_t hitLengthSamples(core::FxHitType type, double beatSeconds,
                                                     double sampleRate) noexcept;

 private:
  static constexpr int kSlots = 2 * kMaxVoices;  // the extra slots hold voices that are fading out after a steal
  static constexpr int kFilterUpdateInterval = 16;

  struct Voice {
    bool used{false};
    bool dying{false};
    core::FxHitType type{core::FxHitType::AirHorn};
    float gain{0.0F};
    float stealGain{1.0F};
    float stealStep{0.0F};
    double beat{kDefaultBeatSeconds};
    std::int64_t n{0};       // samples rendered so far
    std::int64_t length{0};  // total samples of the hit
    std::uint64_t serial{0};
    std::array<double, 8> phase{};
    std::array<std::uint32_t, 2> rng{};
    std::array<StateVariableFilter, 2> svf{};
    std::array<float, 2> lp{};
  };

  void startVoice(Voice& v, core::FxHitType type, float level, double beat) noexcept;
  void renderVoice(Voice& v, float* left, float* right, int numSamples) noexcept;
  void sample(Voice& v, float& l, float& r) noexcept;

  void airHorn(Voice& v, float& l, float& r) noexcept;
  void siren(Voice& v, float& l, float& r) noexcept;
  void sweepNoise(Voice& v, bool rising, float& l, float& r) noexcept;
  void impact(Voice& v, float& l, float& r) noexcept;
  void laser(Voice& v, float& l, float& r) noexcept;

  double sampleRate_{48000.0};
  std::uint64_t nextSerial_{1};
  std::array<Voice, kSlots> voices_{};
};

}  // namespace zyron::audio
