// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "Core/System/HardwareInfo.hpp"

namespace zyron::core {

// One narrow interface per kind of hardware, so each implementation lives in the module that may use the needed
// library (ADR-0003): JUCE probes in Platform/Audio/MIDI, NVML in AI/Backends/Cuda. Tests use fakes.
// probe() is called from one thread at a time. It may be slow (driver enumeration) and must never be called from the
// audio thread. Implementations should report problems through their return value; HardwareDetector also isolates
// exceptions.

class SystemProbe {
 public:
  virtual ~SystemProbe() = default;
  [[nodiscard]] virtual SystemInfo probe() = 0;
};

class GpuProbe {
 public:
  virtual ~GpuProbe() = default;
  [[nodiscard]] virtual GpuInfo probe() = 0;
};

class AudioDeviceProbe {
 public:
  virtual ~AudioDeviceProbe() = default;
  [[nodiscard]] virtual std::vector<AudioDeviceInfo> probe() = 0;
};

struct MidiDevices {
  std::vector<MidiDeviceInfo> inputs;
  std::vector<MidiDeviceInfo> outputs;
};

class MidiProbe {
 public:
  virtual ~MidiProbe() = default;
  [[nodiscard]] virtual MidiDevices probe() = 0;
};

/// Runs the configured probes and assembles a HardwareReport. A missing (null) probe leaves its section empty; a probe
/// that throws is recorded in `warnings` (and GpuInfo::Status::Error for the GPU) without affecting the other sections.
class HardwareDetector {
 public:
  struct Probes {
    std::shared_ptr<SystemProbe> system;
    std::shared_ptr<GpuProbe> gpu;
    std::shared_ptr<AudioDeviceProbe> audio;
    std::shared_ptr<MidiProbe> midi;
  };

  explicit HardwareDetector(Probes probes) : probes_(std::move(probes)) {}

  /// Never throws.
  [[nodiscard]] HardwareReport detect() const;

 private:
  Probes probes_;
};

}  // namespace zyron::core
