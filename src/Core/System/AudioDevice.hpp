// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

namespace zyron::core {

/// Audio device configuration parameters (SPEC sections 45, 80).
struct AudioDeviceConfig {
  std::string deviceName;
  double sampleRate{48000.0};
  int bufferSize{256};
  int inputChannels{0};
  int outputChannels{2};
};

/// Abstract audio device interface (SPEC sections 78, 80).
/// Defines device lifecycle independent of JUCE or OS audio APIs.
class AudioDevice {
 public:
  virtual ~AudioDevice() = default;

  [[nodiscard]] virtual bool open(const AudioDeviceConfig& config) = 0;
  virtual void close() = 0;
  [[nodiscard]] virtual bool isOpen() const = 0;
  [[nodiscard]] virtual AudioDeviceConfig currentConfig() const = 0;
};

}  // namespace zyron::core
