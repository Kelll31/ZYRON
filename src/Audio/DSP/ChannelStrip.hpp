// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>

#include "Audio/DSP/DjFilter.hpp"
#include "Audio/DSP/ThreeBandEq.hpp"
#include "Audio/Effects/EffectSlot.hpp"
#include "Audio/Effects/FxUnit.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Mixer Channel Strip combining input trim, 3-band EQ, DJ filter, FX slots, and fader
/// (ROADMAP P2-03, P2-08, SPEC section 20, 23, ARCHITECTURE section 7).
///
/// Signal order: track trim x gain -> EQ -> DJ filter -> legacy EffectSlots -> pre-fader FxUnits -> fader ->
/// post-fader FxUnits -> meter. An FxUnit after the fader only receives what the fader lets through, so when the DJ
/// closes the fader its echoes and reverb keep ringing out instead of being cut ("echo out").
/// Real-time safe, click-free parameter smoothing, zero allocations on audio thread.
class ChannelStrip {
 public:
  static constexpr int kNumFxSlots = 2;

  ChannelStrip();
  ~ChannelStrip() = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  // Controls
  void setGainDb(float gainDb) noexcept;
  /// Loudness trim of the loaded track (-12..+12 dB), before the gain knob, smoothed over 10 ms (SetTrackGainTrim).
  void setTrackGainTrimDb(float trimDb) noexcept;
  [[nodiscard]] float trackGainTrimDb() const noexcept { return trackTrimDb_.load(std::memory_order_relaxed); }
  void setEqDb(core::EqBand band, float gainDb) noexcept;
  void setLowDb(float gainDb) noexcept;
  void setMidDb(float gainDb) noexcept;
  void setHighDb(float gainDb) noexcept;
  void setFilter(float knobPosition) noexcept;
  void setFilterResonance(float q) noexcept;
  void setVolume(float volumeLinear) noexcept;
  void setMute(bool muted) noexcept;

  // FX slots driven by SetFx / SetFxTempo (preallocated effect banks; audio-thread calls allocate nothing)
  void setFx(int slot, core::FxType type, bool enabled, float wet, float param, bool postFader) noexcept;
  void setFxBeatSeconds(float beatSeconds) noexcept;
  [[nodiscard]] const FxUnit& fxUnit(int slot) const noexcept;

  // Free-form effect slots (an Effect instance set from a non-realtime thread; always before the fader)
  [[nodiscard]] EffectSlot& fxSlot(int slotIndex) noexcept;
  [[nodiscard]] const EffectSlot& fxSlot(int slotIndex) const noexcept;

  /// Processes stereo audio in place. Realtime safe.
  void process(float* const* channels, int numChannels, int numSamples) noexcept;

  // Telemetry (peak meters for UI display)
  [[nodiscard]] float peakLeft() const noexcept;
  [[nodiscard]] float peakRight() const noexcept;

 private:
  double sampleRate_{48000.0};
  float rampCoeff_{0.002F};

  ThreeBandEq eq_;
  DjFilter filter_;
  std::array<EffectSlot, kNumFxSlots> fxSlots_;
  std::array<FxUnit, kNumFxSlots> fxUnits_;

  std::atomic<float> trackTrimDb_{0.0F};
  std::atomic<float> targetTrimLinear_{1.0F};
  float currentTrimLinear_{1.0F};
  std::atomic<float> targetGainLinear_{1.0F};
  std::atomic<float> targetVolumeLinear_{1.0F};
  std::atomic<bool> muted_{false};

  float currentGainLinear_{1.0F};
  float currentVolumeLinear_{1.0F};

  std::atomic<float> peakLeft_{0.0F};
  std::atomic<float> peakRight_{0.0F};
};

}  // namespace zyron::audio
