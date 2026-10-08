// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Core/MIDI/MidiTypes.hpp"
#include "Core/System/HardwareInfo.hpp"

namespace zyron::midi {

/// Manager for MIDI input/output ports, hotplug detection, and message routing (SPEC section 47, ROADMAP P9-01).
class MidiDeviceManager {
 public:
  using MidiCallback = std::function<void(const core::MidiEvent&)>;
  using DeviceChangeCallback = std::function<void(const std::vector<core::MidiDeviceInfo>& inputs,
                                                  const std::vector<core::MidiDeviceInfo>& outputs)>;

  MidiDeviceManager();
  virtual ~MidiDeviceManager();

  /// Refreshes connected hardware list and detects hotplug additions/removals.
  bool refreshDevices();

  [[nodiscard]] std::vector<core::MidiDeviceInfo> availableInputs() const;
  [[nodiscard]] std::vector<core::MidiDeviceInfo> availableOutputs() const;

  /// Opens MIDI input by device identifier (or index).
  virtual bool openInput(const std::string& deviceIdentifier);
  virtual void closeInput(const std::string& deviceIdentifier);
  virtual void closeAllInputs();

  /// Opens MIDI output for LED / hardware state feedback.
  virtual bool openOutput(const std::string& deviceIdentifier);
  virtual void closeOutput(const std::string& deviceIdentifier);
  virtual void closeAllOutputs();

  /// Sends a MIDI event to open output ports (e.g. for button LED on/off).
  virtual bool sendMidi(const core::MidiEvent& event, const std::string& deviceIdentifier = "");

  /// Injects or routes an incoming MIDI event to registered callbacks (thread-safe).
  void handleIncomingMidi(const core::MidiEvent& event);

  void setMidiCallback(MidiCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onMidi_ = std::move(cb);
  }

  void setDeviceChangeCallback(DeviceChangeCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    onDeviceChange_ = std::move(cb);
  }

  [[nodiscard]] std::size_t activeInputCount() const noexcept;
  [[nodiscard]] std::size_t activeOutputCount() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  mutable std::mutex mutex_;
  MidiCallback onMidi_;
  DeviceChangeCallback onDeviceChange_;
};

}  // namespace zyron::midi
