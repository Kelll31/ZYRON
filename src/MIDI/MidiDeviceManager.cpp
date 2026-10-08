// SPDX-License-Identifier: AGPL-3.0-only
#include "MIDI/MidiDeviceManager.hpp"

#include <juce_audio_devices/juce_audio_devices.h>
#include <algorithm>
#include <unordered_map>

namespace zyron::midi {

struct MidiDeviceManager::Impl : public juce::MidiInputCallback {
  MidiDeviceManager& owner;
  std::vector<core::MidiDeviceInfo> lastInputs;
  std::vector<core::MidiDeviceInfo> lastOutputs;

  std::unordered_map<std::string, std::unique_ptr<juce::MidiInput>> activeInputs;
  std::unordered_map<std::string, std::unique_ptr<juce::MidiOutput>> activeOutputs;

  explicit Impl(MidiDeviceManager& o) : owner(o) {}

  void handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& msg) override {
    core::MidiEvent evt;
    evt.channel = msg.getChannel();
    evt.timestampSec = msg.getTimeStamp();

    if (msg.isNoteOn()) {
      evt.type = core::MidiMessageType::NoteOn;
      evt.number = msg.getNoteNumber();
      evt.value = msg.getVelocity();
    } else if (msg.isNoteOff()) {
      evt.type = core::MidiMessageType::NoteOff;
      evt.number = msg.getNoteNumber();
      evt.value = msg.getVelocity();
    } else if (msg.isController()) {
      evt.type = core::MidiMessageType::ControlChange;
      evt.number = msg.getControllerNumber();
      evt.value = msg.getControllerValue();
    } else if (msg.isPitchWheel()) {
      evt.type = core::MidiMessageType::PitchBend;
      evt.number = 0;
      evt.value = msg.getPitchWheelValue();
    } else {
      return;  // Ignore clock/sysex for DJ mapping
    }

    owner.handleIncomingMidi(evt);
  }
};

MidiDeviceManager::MidiDeviceManager()
    : impl_(std::make_unique<Impl>(*this)) {
  refreshDevices();
}

MidiDeviceManager::~MidiDeviceManager() {
  closeAllInputs();
  closeAllOutputs();
  impl_.reset();
  juce::DeletedAtShutdown::deleteAll();
}

bool MidiDeviceManager::refreshDevices() {
  std::vector<core::MidiDeviceInfo> newInputs;
  std::vector<core::MidiDeviceInfo> newOutputs;

  const auto juceInputs = juce::MidiInput::getAvailableDevices();
  for (const auto& d : juceInputs) {
    core::MidiDeviceInfo info;
    info.identifier = d.identifier.toStdString();
    info.name = d.name.toStdString();
    newInputs.push_back(std::move(info));
  }

  const auto juceOutputs = juce::MidiOutput::getAvailableDevices();
  for (const auto& d : juceOutputs) {
    core::MidiDeviceInfo info;
    info.identifier = d.identifier.toStdString();
    info.name = d.name.toStdString();
    newOutputs.push_back(std::move(info));
  }

  auto sameDevices = [](const std::vector<core::MidiDeviceInfo>& a, const std::vector<core::MidiDeviceInfo>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (a[i].identifier != b[i].identifier || a[i].name != b[i].name) return false;
    }
    return true;
  };

  bool changed = false;
  DeviceChangeCallback cb;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sameDevices(newInputs, impl_->lastInputs) || !sameDevices(newOutputs, impl_->lastOutputs)) {
      changed = true;
      impl_->lastInputs = newInputs;
      impl_->lastOutputs = newOutputs;
      cb = onDeviceChange_;
    }
  }

  if (changed && cb) {
    cb(newInputs, newOutputs);
  }

  return changed;
}

std::vector<core::MidiDeviceInfo> MidiDeviceManager::availableInputs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->lastInputs;
}

std::vector<core::MidiDeviceInfo> MidiDeviceManager::availableOutputs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->lastOutputs;
}

bool MidiDeviceManager::openInput(const std::string& deviceIdentifier) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (impl_->activeInputs.find(deviceIdentifier) != impl_->activeInputs.end()) {
    return true;  // Already open
  }

  auto input = juce::MidiInput::openDevice(deviceIdentifier, impl_.get());
  if (!input) {
    return false;
  }

  input->start();
  impl_->activeInputs[deviceIdentifier] = std::move(input);
  return true;
}

void MidiDeviceManager::closeInput(const std::string& deviceIdentifier) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = impl_->activeInputs.find(deviceIdentifier);
  if (it != impl_->activeInputs.end()) {
    if (it->second) {
      it->second->stop();
    }
    impl_->activeInputs.erase(it);
  }
}

void MidiDeviceManager::closeAllInputs() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [_, in] : impl_->activeInputs) {
    if (in) in->stop();
  }
  impl_->activeInputs.clear();
}

bool MidiDeviceManager::openOutput(const std::string& deviceIdentifier) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (impl_->activeOutputs.find(deviceIdentifier) != impl_->activeOutputs.end()) {
    return true;
  }

  auto out = juce::MidiOutput::openDevice(deviceIdentifier);
  if (!out) {
    return false;
  }

  impl_->activeOutputs[deviceIdentifier] = std::move(out);
  return true;
}

void MidiDeviceManager::closeOutput(const std::string& deviceIdentifier) {
  std::lock_guard<std::mutex> lock(mutex_);
  impl_->activeOutputs.erase(deviceIdentifier);
}

void MidiDeviceManager::closeAllOutputs() {
  std::lock_guard<std::mutex> lock(mutex_);
  impl_->activeOutputs.clear();
}

bool MidiDeviceManager::sendMidi(const core::MidiEvent& event, const std::string& deviceIdentifier) {
  std::lock_guard<std::mutex> lock(mutex_);

  juce::MidiMessage msg;
  switch (event.type) {
    case core::MidiMessageType::NoteOn:
      msg = juce::MidiMessage::noteOn(event.channel, event.number, static_cast<juce::uint8>(std::clamp(event.value, 0, 127)));
      break;
    case core::MidiMessageType::NoteOff:
      msg = juce::MidiMessage::noteOff(event.channel, event.number, static_cast<juce::uint8>(std::clamp(event.value, 0, 127)));
      break;
    case core::MidiMessageType::ControlChange:
      msg = juce::MidiMessage::controllerEvent(event.channel, event.number, static_cast<juce::uint8>(std::clamp(event.value, 0, 127)));
      break;
    case core::MidiMessageType::PitchBend:
      msg = juce::MidiMessage::pitchWheel(event.channel, std::clamp(event.value, 0, 16383));
      break;
  }

  if (!deviceIdentifier.empty()) {
    auto it = impl_->activeOutputs.find(deviceIdentifier);
    if (it != impl_->activeOutputs.end() && it->second) {
      it->second->sendMessageNow(msg);
      return true;
    }
    return false;
  }

  bool sentAny = false;
  for (auto& [_, out] : impl_->activeOutputs) {
    if (out) {
      out->sendMessageNow(msg);
      sentAny = true;
    }
  }
  return sentAny;
}

void MidiDeviceManager::handleIncomingMidi(const core::MidiEvent& event) {
  MidiCallback cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cb = onMidi_;
  }
  if (cb) {
    cb(event);
  }
}

std::size_t MidiDeviceManager::activeInputCount() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->activeInputs.size();
}

std::size_t MidiDeviceManager::activeOutputCount() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->activeOutputs.size();
}

}  // namespace zyron::midi
