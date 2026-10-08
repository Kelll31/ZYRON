// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <string>

namespace zyron::core {

/// What the audio engine actually obtained and how it is doing - the facts, as opposed to AudioOutputSettings which is
/// what the user asked for (ARCHITECTURE section 6: telemetry, not state). Polled by the UI a few times per second.
struct AudioEngineStats {
  bool deviceOpen{false};
  std::string apiName;
  std::string deviceName;
  double sampleRate{0.0};
  int bufferSize{0};  // frames per callback
  int outputChannels{0};
  double outputLatencyMs{0.0};
  std::uint64_t callbackCount{0};
  int xrunCount{-1};      // buffer under/overruns reported by the audio API; -1 = the API does not report them
  double cpuLoad{0.0};    // share of the buffer time spent in the callback, 0..1
  std::string lastError;  // empty when the last attempt to open a device succeeded
};

/// Implemented by the audio engine. stats() must be called from the thread that owns the device (the JUCE message
/// thread); it is cheap but not realtime-safe.
class AudioEngineStatsSource {
 public:
  virtual ~AudioEngineStatsSource() = default;
  [[nodiscard]] virtual AudioEngineStats stats() const = 0;
};

/// Multi-line English summary for the settings panel and `--audio-selftest`.
[[nodiscard]] std::string formatEngineStats(const AudioEngineStats& stats);

}  // namespace zyron::core
