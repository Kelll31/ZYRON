// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace zyron::audio {

/// Descriptor for an effect parameter.
struct ParameterDescriptor {
  std::string_view id;
  std::string_view name;
  float minValue{0.0F};
  float maxValue{1.0F};
  float defaultValue{0.0F};
  std::string_view unit;  // e.g. "ms", "Hz", "%", "dB"
};

/// Base interface for all audio effects (SPEC section 23, ARCHITECTURE section 7).
/// Strict realtime contract: prepare() and reset() allocate/initialize offline.
/// process() is noexcept, zero-allocation, lock-free, and bounded.
class Effect {
 public:
  virtual ~Effect() = default;

  [[nodiscard]] virtual std::string_view id() const noexcept = 0;
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  /// Allocates delay buffers, initializes sample-rate dependent filters.
  /// Called offline before audio processing starts.
  virtual void prepare(double sampleRate, int maxBlockSize) noexcept = 0;

  /// Clears delay lines and filter histories without reallocating.
  virtual void reset() noexcept = 0;

  /// Processes stereo/mono audio in place. Realtime safe.
  virtual void process(float* const* channels, int numChannels, int numSamples) noexcept = 0;

  // Bypass / dry-wet mix controls
  virtual void setEnabled(bool enabled) noexcept = 0;
  [[nodiscard]] virtual bool isEnabled() const noexcept = 0;

  virtual void setDryWet(float mixLinear) noexcept = 0;  // 0.0 (dry) .. 1.0 (wet)
  [[nodiscard]] virtual float dryWet() const noexcept = 0;

  // Generic parameter reflection and control
  [[nodiscard]] virtual std::span<const ParameterDescriptor> parameters() const noexcept = 0;
  virtual void setParameter(std::size_t index, float value) noexcept = 0;
  [[nodiscard]] virtual float parameter(std::size_t index) const noexcept = 0;

  void setParameterById(std::string_view paramId, float value) noexcept {
    const auto params = parameters();
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (params[i].id == paramId) {
        setParameter(i, value);
        return;
      }
    }
  }

  [[nodiscard]] float parameterById(std::string_view paramId) const noexcept {
    const auto params = parameters();
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (params[i].id == paramId) {
        return parameter(i);
      }
    }
    return 0.0F;
  }
};

}  // namespace zyron::audio
