// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <atomic>

#include "Audio/DSP/DjFilter.hpp"
#include "Audio/DSP/ThreeBandEq.hpp"
#include "Audio/Effects/EffectSlot.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Mixer Channel Strip combining input trim, 3-band EQ, DJ filter, FX slots, and fader
/// (ROADMAP P2-03, P2-08, SPEC section 20, 23, ARCHITECTURE section 7).
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
  void setEqDb(core::EqBand band, float gainDb) noexcept;
  void setLowDb(float gainDb) noexcept;
  void setMidDb(float gainDb) noexcept;
  void setHighDb(float gainDb) noexcept;
  void setFilter(float knobPosition) noexcept;
  void setFilterResonance(float q) noexcept;
  void setVolume(float volumeLinear) noexcept;
  void setMute(bool muted) noexcept;

  // FX slots
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

  std::atomic<float> targetGainLinear_{1.0F};
  std::atomic<float> targetVolumeLinear_{1.0F};
  std::atomic<bool> muted_{false};

  float currentGainLinear_{1.0F};
  float currentVolumeLinear_{1.0F};

  std::atomic<float> peakLeft_{0.0F};
  std::atomic<float> peakRight_{0.0F};
};

}  // namespace zyron::audio
