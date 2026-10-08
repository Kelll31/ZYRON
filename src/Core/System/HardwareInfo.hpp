// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zyron::core {

// Plain data describing the machine ZYRON runs on (SPEC section 64). Filled by probes (HardwareProbe.hpp), shown by
// the diagnostics panel and written to logs. No behaviour here beyond formatting.

struct OsInfo {
  std::string name;          // e.g. "Windows 11"
  std::string architecture;  // e.g. "64-bit"
};

struct CpuInfo {
  std::string model;
  unsigned logicalCores{0};
  unsigned physicalCores{0};
};

struct MemoryInfo {
  std::uint64_t totalBytes{0};
};

struct SystemInfo {
  OsInfo os;
  CpuInfo cpu;
  MemoryInfo memory;
};

struct GpuDevice {
  /// The number the probe's own library gives the device (NVML: PCI bus order). It is NOT necessarily the ordinal a
  /// compute API uses (CUDA's default order is fastest-first, and CUDA_VISIBLE_DEVICES remaps it), so a backend must
  /// map by name/UUID/bus id rather than reuse this as a CUDA device number.
  int index{0};
  std::string name;
  std::uint64_t vramTotalBytes{0};  // 0 = unknown
  std::uint64_t vramFreeBytes{0};
  std::string computeCapability;  // vendor-specific text such as "8.6" (CUDA); empty = unknown
};

struct GpuInfo {
  enum class Status : std::uint8_t {
    NotProbed,  // no GPU probe was configured
    Available,  // at least one device was found
    NoDriver,   // the vendor's management library is not installed: normal on machines without that vendor's GPU
    NoDevices,  // the library works but reports zero devices
    Error,      // the library is present but a call failed; see `detail`
  };

  Status status{Status::NotProbed};
  std::string
      detail;  // reason when status != Available; when Available, non-fatal notes (e.g. "device 1 not accessible")
  std::string vendor;         // e.g. "NVIDIA"; filled by the probe, so Core never has to know vendor specifics
  std::string driverVersion;  // e.g. "576.52"; empty = unknown
  std::string computeApi;     // e.g. "CUDA 13.4"; empty = unknown
  std::vector<GpuDevice> devices;
};

struct AudioDeviceInfo {
  std::string apiName;  // audio API / driver type, e.g. "Windows Audio", "ASIO", "CoreAudio", "ALSA"
  std::string name;
  bool isInput{false};   // the device has capture ports
  bool isOutput{false};  // the device has playback ports
  bool isDefaultInput{false};
  bool isDefaultOutput{false};

  // Filled only when `capabilitiesKnown`: finding them out means creating the device, which is slow for drivers with
  // many endpoints (about 100 ms each on Windows), so the quick startup scan leaves them out.
  bool capabilitiesKnown{false};
  int inputChannels{0};
  int outputChannels{0};
  std::vector<double> sampleRates;
  std::vector<int> bufferSizes;
  int defaultBufferSize{0};
};

struct MidiDeviceInfo {
  std::string name;
  std::string identifier;
};

struct HardwareReport {
  SystemInfo system;
  GpuInfo gpu;
  std::vector<AudioDeviceInfo> audioDevices;
  std::vector<MidiDeviceInfo> midiInputs;
  std::vector<MidiDeviceInfo> midiOutputs;
  std::vector<std::string> warnings;  // probes that failed; the rest of the report is still valid
};

/// Binary units: "0 B", "1.5 KiB", "24.0 GiB". Locale-independent.
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);

/// Multi-line, English; used by the diagnostics panel, logs and `--print-hardware`. The framing is plain ASCII;
/// device names are passed through as the UTF-8 the OS reports them in.
[[nodiscard]] std::string formatReport(const HardwareReport& report);

}  // namespace zyron::core
