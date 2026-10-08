// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Routing/JuceAudioDeviceProbe.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace zyron::audio {
namespace {

bool isDefault(const juce::StringArray& names, int defaultIndex, const juce::String& name) {
  return defaultIndex >= 0 && defaultIndex < names.size() && names[defaultIndex] == name;
}

void queryCapabilities(juce::AudioIODeviceType& type, const juce::String& outName, const juce::String& inName,
                       core::AudioDeviceInfo& info) {
  const std::unique_ptr<juce::AudioIODevice> device(type.createDevice(outName, inName));
  if (device == nullptr) {
    return;  // listed but not creatable (driver gone, device busy): keep the entry, without capabilities
  }
  info.capabilitiesKnown = true;
  info.outputChannels = device->getOutputChannelNames().size();
  info.inputChannels = device->getInputChannelNames().size();
  for (const double rate : device->getAvailableSampleRates()) {
    info.sampleRates.push_back(rate);
  }
  for (const int size : device->getAvailableBufferSizes()) {
    info.bufferSizes.push_back(size);
  }
  info.defaultBufferSize = device->getDefaultBufferSize();
}

}  // namespace

std::vector<core::AudioDeviceInfo> JuceAudioDeviceProbe::probe() {
  juce::AudioDeviceManager manager;  // only used as a factory for the audio API types; no device is opened
  juce::OwnedArray<juce::AudioIODeviceType> types;
  manager.createAudioDeviceTypes(types);

  std::vector<core::AudioDeviceInfo> result;
  for (juce::AudioIODeviceType* type : types) {
    type->scanForDevices();
    const juce::StringArray outputs = type->getDeviceNames(false);
    const juce::StringArray inputs = type->getDeviceNames(true);
    const int defaultOutput = type->getDefaultDeviceIndex(false);
    const int defaultInput = type->getDefaultDeviceIndex(true);

    juce::StringArray names = outputs;
    names.addArray(inputs);
    names.removeDuplicates(false);  // a duplex device appears in both lists

    for (const juce::String& name : names) {
      core::AudioDeviceInfo info;
      info.apiName = type->getTypeName().toStdString();
      info.name = name.toStdString();
      info.isOutput = outputs.contains(name);
      info.isInput = inputs.contains(name);
      info.isDefaultOutput = isDefault(outputs, defaultOutput, name);
      info.isDefaultInput = isDefault(inputs, defaultInput, name);

      if (detail_ == Detail::WithCapabilities) {
        queryCapabilities(*type, info.isOutput ? name : juce::String(), info.isInput ? name : juce::String(), info);
      }
      result.push_back(std::move(info));
    }
  }
  return result;
}

}  // namespace zyron::audio
