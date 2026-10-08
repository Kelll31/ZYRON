// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/Common/JuceSystemProbe.hpp"

#include <juce_core/juce_core.h>

namespace zyron::platform {

core::SystemInfo JuceSystemProbe::probe() {
  core::SystemInfo info;
  info.os.name = juce::SystemStats::getOperatingSystemName().toStdString();
  info.os.architecture = juce::SystemStats::isOperatingSystem64Bit() ? "64-bit" : "32-bit";
  info.cpu.model = juce::SystemStats::getCpuModel().toStdString();
  info.cpu.logicalCores = static_cast<unsigned>(juce::SystemStats::getNumCpus());
  info.cpu.physicalCores = static_cast<unsigned>(juce::SystemStats::getNumPhysicalCpus());
  info.memory.totalBytes =
      static_cast<std::uint64_t>(juce::SystemStats::getMemorySizeInMegabytes()) * 1024ULL * 1024ULL;
  return info;
}

}  // namespace zyron::platform
