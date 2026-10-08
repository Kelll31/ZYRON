// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <memory>

#include "Audio/Effects/Effect.hpp"

namespace zyron::audio {

/// Represents an FX slot inserted in the channel strip or master signal chain (ARCHITECTURE section 7).
/// Realtime safe: holds an effect instance and routes audio through it when active.
class EffectSlot {
 public:
  EffectSlot() = default;
  ~EffectSlot() = default;

  EffectSlot(const EffectSlot&) = delete;
  EffectSlot& operator=(const EffectSlot&) = delete;
  EffectSlot(EffectSlot&&) noexcept = default;
  EffectSlot& operator=(EffectSlot&&) noexcept = default;

  void prepare(double sampleRate, int maxBlockSize) noexcept;
  void reset() noexcept;

  /// Sets an effect on this slot. Called from control/UI threads.
  void setEffect(std::unique_ptr<Effect> effect) noexcept;
  [[nodiscard]] Effect* effect() const noexcept;

  void setEnabled(bool enabled) noexcept;
  [[nodiscard]] bool isEnabled() const noexcept;

  void setDryWet(float mixLinear) noexcept;
  [[nodiscard]] float dryWet() const noexcept;

  /// In-place realtime audio processing.
  void process(float* const* channels, int numChannels, int numSamples) noexcept;

 private:
  double sampleRate_{48000.0};
  int maxBlockSize_{1024};

  std::unique_ptr<Effect> effect_{nullptr};
  std::atomic<Effect*> activeEffect_{nullptr};
  std::atomic<bool> enabled_{true};
  std::atomic<float> dryWet_{1.0F};
};

}  // namespace zyron::audio
