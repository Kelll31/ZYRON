// SPDX-License-Identifier: AGPL-3.0-only
#include "Core/System/HardwareInfo.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>

#include "Core/System/HardwareProbe.hpp"

namespace zyron::core {
namespace {

constexpr int kLabelWidth = 13;                   // widest label: "Audio devices"
constexpr std::size_t kMaxListedBufferSizes = 4;  // longer lists are shown as a range

std::ostringstream makeStream() {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  return out;
}

void line(std::ostringstream& out, std::string_view label, const std::string& value) {
  out << std::left << std::setw(kLabelWidth) << label << ": " << value << '\n';
}

template <class T>
std::string join(const std::vector<T>& values, const char* separator) {
  auto out = makeStream();
  bool first = true;
  for (const T& value : values) {
    out << (first ? "" : separator) << value;
    first = false;
  }
  return out.str();
}

std::string describeAudioDevice(const AudioDeviceInfo& device) {
  auto out = makeStream();
  out << "  " << device.apiName << " | " << device.name << " |";
  if (device.isOutput) {
    out << " out";
    if (device.capabilitiesKnown) {
      out << ' ' << device.outputChannels << "ch";
    }
    out << (device.isDefaultOutput ? " (default)" : "");
  }
  if (device.isInput) {
    out << " in";
    if (device.capabilitiesKnown) {
      out << ' ' << device.inputChannels << "ch";
    }
    out << (device.isDefaultInput ? " (default)" : "");
  }
  if (!device.capabilitiesKnown) {
    return out.str();
  }
  std::vector<long long> rates;
  rates.reserve(device.sampleRates.size());
  for (const double rate : device.sampleRates) {
    rates.push_back(std::llround(rate));
  }
  if (!rates.empty()) {
    out << " | " << join(rates, ", ") << " Hz";
  }
  if (!device.bufferSizes.empty()) {
    out << " | buffers ";
    const bool hasDefault = device.defaultBufferSize > 0;
    if (device.bufferSizes.size() > kMaxListedBufferSizes) {
      const auto [low, high] = std::minmax_element(device.bufferSizes.begin(), device.bufferSizes.end());
      out << *low << '-' << *high << " (" << device.bufferSizes.size() << " sizes"
          << (hasDefault ? ", default " + std::to_string(device.defaultBufferSize) : std::string{}) << ")";
    } else {
      out << join(device.bufferSizes, ", ");
      if (hasDefault) {
        out << " (default " << device.defaultBufferSize << ")";
      }
    }
  }
  return out.str();
}

void describeMidi(std::ostringstream& out, std::string_view label, const std::vector<MidiDeviceInfo>& devices) {
  line(out, label, devices.empty() ? "none" : std::to_string(devices.size()));
  for (const MidiDeviceInfo& device : devices) {
    out << "  " << device.name << '\n';
  }
}

void describeGpu(std::ostringstream& out, const GpuInfo& gpu) {
  switch (gpu.status) {
    case GpuInfo::Status::NotProbed:
      line(out, "GPU", "not probed");
      return;
    case GpuInfo::Status::Available: {
      auto head = makeStream();
      head << gpu.devices.size() << ' ' << (gpu.vendor.empty() ? "" : gpu.vendor + " ") << "GPU(s)";
      if (!gpu.driverVersion.empty()) {
        head << ", driver " << gpu.driverVersion;
      }
      if (!gpu.computeApi.empty()) {
        head << ", " << gpu.computeApi;
      }
      line(out, "GPU", head.str());
      for (const GpuDevice& device : gpu.devices) {
        out << "  [" << device.index << "] " << device.name << "  ";
        if (device.vramTotalBytes == 0) {
          out << "VRAM unknown";
        } else {
          out << formatBytes(device.vramTotalBytes) << " (" << formatBytes(device.vramFreeBytes) << " free)";
        }
        out << "  compute " << (device.computeCapability.empty() ? "unknown" : device.computeCapability) << '\n';
      }
      if (!gpu.detail.empty()) {
        out << "  note: " << gpu.detail << '\n';
      }
      return;
    }
    case GpuInfo::Status::NoDriver:
    case GpuInfo::Status::NoDevices:
    case GpuInfo::Status::Error:
      line(out, "GPU", "no supported GPU available (" + gpu.detail + "); AI will use the CPU backend");
      return;
  }
}

}  // namespace

std::string formatBytes(std::uint64_t bytes) {
  constexpr const char* kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
  constexpr std::size_t kUnitCount = std::size(kUnits);
  if (bytes < 1024) {
    return std::to_string(bytes) + " B";
  }
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < kUnitCount) {
    value /= 1024.0;
    ++unit;
  }
  // 1048575 B is 1023.999 KiB, which would print as "1024.0 KiB"; promote it to the next unit instead.
  if (std::round(value * 10.0) / 10.0 >= 1024.0 && unit + 1 < kUnitCount) {
    value /= 1024.0;
    ++unit;
  }
  auto out = makeStream();
  out << std::fixed << std::setprecision(1) << value << ' ' << kUnits[unit];
  return out.str();
}

std::string formatReport(const HardwareReport& report) {
  auto out = makeStream();
  out << "ZYRON hardware report\n---------------------\n";

  const SystemInfo& system = report.system;
  line(out, "OS", system.os.name.empty() ? "unknown" : system.os.name + " (" + system.os.architecture + ")");
  line(out, "CPU",
       system.cpu.model.empty() ? "unknown"
                                : system.cpu.model + " (" + std::to_string(system.cpu.logicalCores) + " logical / " +
                                      std::to_string(system.cpu.physicalCores) + " physical cores)");
  line(out, "Memory", formatBytes(system.memory.totalBytes));

  describeGpu(out, report.gpu);

  line(out, "Audio devices", report.audioDevices.empty() ? "none" : std::to_string(report.audioDevices.size()));
  for (const AudioDeviceInfo& device : report.audioDevices) {
    out << describeAudioDevice(device) << '\n';
  }

  describeMidi(out, "MIDI inputs", report.midiInputs);
  describeMidi(out, "MIDI outputs", report.midiOutputs);

  if (!report.warnings.empty()) {
    out << "Warnings:\n";
    for (const std::string& warning : report.warnings) {
      out << "  - " << warning << '\n';
    }
  }
  return out.str();
}

HardwareReport HardwareDetector::detect() const {
  HardwareReport report;

  const auto guarded = [&report](const char* what, auto&& run) {
    try {
      run();
    } catch (const std::exception& error) {
      report.warnings.push_back(std::string(what) + " probe failed: " + error.what());
    } catch (...) {
      report.warnings.push_back(std::string(what) + " probe failed: unknown error");
    }
  };

  if (probes_.system) {
    guarded("system", [&] { report.system = probes_.system->probe(); });
  }
  if (probes_.gpu) {
    const std::size_t before = report.warnings.size();
    guarded("GPU", [&] { report.gpu = probes_.gpu->probe(); });
    if (report.warnings.size() > before) {
      report.gpu.status = GpuInfo::Status::Error;
      report.gpu.detail = report.warnings.back();
    }
  }
  if (probes_.audio) {
    guarded("audio", [&] { report.audioDevices = probes_.audio->probe(); });
  }
  if (probes_.midi) {
    guarded("MIDI", [&] {
      MidiDevices devices = probes_.midi->probe();
      report.midiInputs = std::move(devices.inputs);
      report.midiOutputs = std::move(devices.outputs);
    });
  }
  return report;
}

}  // namespace zyron::core
