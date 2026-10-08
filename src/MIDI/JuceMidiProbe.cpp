// SPDX-License-Identifier: AGPL-3.0-only
#include "MIDI/JuceMidiProbe.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

namespace zyron::midi {

core::MidiDevices JuceMidiProbe::probe() {
  core::MidiDevices devices;
  for (const juce::MidiDeviceInfo& device : juce::MidiInput::getAvailableDevices()) {
    devices.inputs.push_back({device.name.toStdString(), device.identifier.toStdString()});
  }
  for (const juce::MidiDeviceInfo& device : juce::MidiOutput::getAvailableDevices()) {
    devices.outputs.push_back({device.name.toStdString(), device.identifier.toStdString()});
  }
  return devices;
}

}  // namespace zyron::midi
