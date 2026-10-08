// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <vector>

#include "Core/System/HardwareProbe.hpp"

namespace zyron::audio {

/// Lists the audio devices of every audio API JUCE offers on this OS (WASAPI/DirectSound/ASIO, CoreAudio,
/// ALSA/JACK...). It never opens a device.
///
/// Call it from the JUCE message thread: several audio APIs require that.
class JuceAudioDeviceProbe final : public core::AudioDeviceProbe {
 public:
  enum class Detail {
    /// Names, direction and which device is the default. Fast (a scan of the drivers); used at startup.
    NamesOnly,
    /// Also channel counts, sample rates and buffer sizes. Needs a device object per endpoint, which took ~3 s for
    /// 26 endpoints on Windows - fine for a diagnostic dump or the settings page, not for startup.
    WithCapabilities,
  };

  explicit JuceAudioDeviceProbe(Detail detail = Detail::NamesOnly) : detail_(detail) {}

  [[nodiscard]] std::vector<core::AudioDeviceInfo> probe() override;

 private:
  Detail detail_;
};

}  // namespace zyron::audio
