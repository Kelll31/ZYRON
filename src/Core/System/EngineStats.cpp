// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/System/EngineStats.hpp"

#include <iomanip>
#include <locale>
#include <sstream>

namespace zyron::core {

std::string formatEngineStats(const AudioEngineStats& stats) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(1);

  if (!stats.deviceOpen) {
    out << "No audio device open";
    if (!stats.lastError.empty()) {
      out << "\nError: " << stats.lastError;
    }
    return out.str();
  }

  out << "Device    : " << stats.apiName << " / " << stats.deviceName << '\n';
  out << "Format    : " << static_cast<long long>(stats.sampleRate + 0.5) << " Hz, " << stats.bufferSize << " frames";
  if (stats.sampleRate > 0.0) {
    out << " (" << (1000.0 * stats.bufferSize / stats.sampleRate) << " ms)";
  }
  out << ", " << stats.outputChannels << " out\n";
  out << "Latency   : " << stats.outputLatencyMs << " ms output\n";
  out << "Callbacks : " << stats.callbackCount << '\n';
  out << "Xruns     : ";
  if (stats.xrunCount < 0) {
    out << "n/a (not reported by this audio API)\n";
  } else {
    out << stats.xrunCount << '\n';
  }
  out << "DSP load  : " << (stats.cpuLoad * 100.0) << " %";
  if (!stats.lastError.empty()) {
    out << "\nError: " << stats.lastError;
  }
  return out.str();
}

}  // namespace zyron::core
