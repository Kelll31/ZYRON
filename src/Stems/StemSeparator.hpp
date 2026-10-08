// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "Stems/StemTypes.hpp"

namespace zyron::stems {

/// Abstract stem separation engine interface (SPEC sections 32, 33, ROADMAP P5-01).
/// Implementations isolate vocals, drums, bass, and other musical components from mixed audio.
class StemSeparator {
 public:
  virtual ~StemSeparator() = default;

  [[nodiscard]] virtual std::string modelName() const = 0;
  [[nodiscard]] virtual std::string modelVersion() const = 0;
  [[nodiscard]] virtual double requiredSampleRate() const = 0;

  /// Separates mixed input audio into 4 stems.
  /// \param inputChannels Array of pointers to planar float channel buffers (e.g. [left, right]).
  /// \param numChannels Number of input channels (typically 2).
  /// \param numFrames Number of audio sample frames to separate.
  /// \param sampleRate Sampling frequency of input audio.
  /// \param progressCallback Optional callback receiving progress in [0.0, 1.0].
  /// \return Separation result containing 4 StemBuffers.
  virtual StemSeparationResult separate(const float* const* inputChannels,
                                        int numChannels,
                                        std::int64_t numFrames,
                                        double sampleRate,
                                        std::function<void(float progress)> progressCallback = nullptr) = 0;
};

}  // namespace zyron::stems
