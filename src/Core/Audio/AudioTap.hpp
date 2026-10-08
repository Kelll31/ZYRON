// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace zyron::core {

/// Realtime-safe audio tap interface (ARCHITECTURE section 7, SPEC section 46).
/// Implementations must never allocate, lock, throw, or perform file I/O in writeSamples.
class IAudioTap {
 public:
  virtual ~IAudioTap() = default;

  /// Called from the realtime audio thread. Must be lock-free and non-blocking.
  virtual void writeSamples(const float* const* channels, int numChannels, int numSamples) noexcept = 0;
};

}  // namespace zyron::core
