// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Engine/AudioEngine.hpp"

#include <functional>
#include <variant>

namespace zyron::audio {

// The generator's clamping ranges and the Command API's validation ranges are two views of the same limits.
static_assert(TestToneGenerator::kMinFrequencyHz == core::limits::kToneFrequencyMinHz);
static_assert(TestToneGenerator::kMaxFrequencyHz == core::limits::kToneFrequencyMaxHz);
static_assert(TestToneGenerator::kMinLevelDb == core::limits::kToneLevelMinDb);
static_assert(TestToneGenerator::kMaxLevelDb == core::limits::kToneLevelMaxDb);

AudioEngine::AudioEngine() : alive_(std::make_shared<std::atomic<bool>>(true)) {}

AudioEngine::~AudioEngine() {
  JUCE_ASSERT_MESSAGE_THREAD  // alive_ is only sound if queued work and destruction share the message thread
      alive_->store(false);   // queued message-thread work becomes a no-op
  stop();
}

void AudioEngine::start(const core::AudioOutputSettings& requested) {
  JUCE_ASSERT_MESSAGE_THREAD
  stage_.tone().setSuspended(false);
  deviceManager_.removeAudioCallback(this);  // idempotent; keeps a second start() from registering twice
  deviceManager_.addAudioCallback(this);

  juce::AudioDeviceManager::AudioDeviceSetup preferred;
  preferred.sampleRate = requested.sampleRate;
  preferred.bufferSize = requested.bufferSize;
  const juce::String error = deviceManager_.initialise(
      /*numInputChannelsNeeded=*/0, /*numOutputChannelsNeeded=*/2, /*savedState=*/nullptr,
      /*selectDefaultDeviceOnFailure=*/true, juce::String::fromUTF8(requested.deviceName.c_str()), &preferred);
  lastError_ = error.toStdString();

  // initialise() opened the default (or preferred) device of the default API; honour an explicit API choice too.
  if (!requested.apiName.empty() &&
      deviceManager_.getCurrentAudioDeviceType() != juce::String::fromUTF8(requested.apiName.c_str())) {
    applyOutputSettings(requested);
  }
}

void AudioEngine::stop() {
  JUCE_ASSERT_MESSAGE_THREAD
  fadeOutAndWait();  // never cut a sine mid-waveform
  deviceManager_.removeAudioCallback(this);
  deviceManager_.closeAudioDevice();
}

void AudioEngine::fadeOutAndWait() {
  stage_.tone().setSuspended(true);
  if (deviceManager_.getCurrentAudioDevice() == nullptr) {
    return;  // nothing is calling us, so nothing will ever publish silence
  }
  const juce::uint32 deadline = juce::Time::getMillisecondCounter() + kFadeOutTimeoutMs;
  while (!stage_.tone().isFadedOut() && juce::Time::getMillisecondCounter() < deadline) {
    juce::Thread::sleep(2);  // message thread, shutdown / device-change path only; bounded by the deadline
  }
}

void AudioEngine::onAttach(const core::AppState& current) noexcept {
  applyTestTone(current.testTone);
}

void AudioEngine::onCommand(const core::Command& command, const core::CommandOrigin&) noexcept {
  if (const auto* tone = std::get_if<core::SetTestTone>(&command)) {
    applyTestTone(core::TestToneState{tone->enabled, tone->frequencyHz, tone->levelDb});
    return;
  }
  if (const auto* output = std::get_if<core::SetAudioOutput>(&command)) {
    // Opening a device takes tens to hundreds of milliseconds: never on the caller's thread.
    (void)postToMessageThread(
        [this, settings = core::AudioOutputSettings{output->apiName, output->deviceName, output->sampleRate,
                                                    output->bufferSize}] { applyOutputSettings(settings); });
  }
}

bool AudioEngine::postToMessageThread(std::function<void()> work) noexcept {
  try {
    const bool queued = juce::MessageManager::callAsync([alive = alive_, work = std::move(work)] {
      if (alive->load()) {
        work();
      }
    });
    if (!queued) {  // the message manager is gone or shutting down
      droppedRequests_.fetch_add(1);
    }
    return queued;
  } catch (...) {
    droppedRequests_.fetch_add(1);  // out of memory: the request is lost, but the audio thread is untouched
    return false;
  }
}

void AudioEngine::applyTestTone(const core::TestToneState& tone) noexcept {
  // One word, one snapshot: the audio thread never sees a mix of old and new values (TestToneGenerator).
  stage_.tone().setFrequencyHz(tone.frequencyHz);
  stage_.tone().setLevelDb(tone.levelDb);
  stage_.tone().setEnabled(tone.enabled);
}

juce::String AudioEngine::defaultOutputDeviceName() const {
  if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
    const juce::StringArray names = type->getDeviceNames(false);
    const int index = type->getDefaultDeviceIndex(false);
    if (index >= 0 && index < names.size()) {
      return names[index];
    }
  }
  return {};
}

void AudioEngine::applyOutputSettings(const core::AudioOutputSettings& requested) {
  JUCE_ASSERT_MESSAGE_THREAD
  lastError_.clear();
  fadeOutAndWait();  // the old device goes quiet before it is closed; the new one fades in from silence

  const juce::String api = juce::String::fromUTF8(requested.apiName.c_str());
  if (api.isNotEmpty() && deviceManager_.getCurrentAudioDeviceType() != api) {
    deviceManager_.setCurrentAudioDeviceType(api, /*treatAsChosenDevice=*/true);
  }

  juce::AudioDeviceManager::AudioDeviceSetup setup;
  deviceManager_.getAudioDeviceSetup(setup);
  setup.outputDeviceName =
      requested.deviceName.empty() ? defaultOutputDeviceName() : juce::String::fromUTF8(requested.deviceName.c_str());
  setup.sampleRate = requested.sampleRate;
  setup.bufferSize = requested.bufferSize;
  setup.useDefaultOutputChannels = true;
  setup.useDefaultInputChannels = false;
  setup.inputChannels.clear();
  setup.inputDeviceName = {};

  const juce::String error = deviceManager_.setAudioDeviceSetup(setup, /*treatAsChosenDevice=*/true);
  lastError_ = error.toStdString();
  stage_.tone().setSuspended(false);  // prepare() restarted the generator at gain 0, so the tone fades in
}

core::AudioEngineStats AudioEngine::stats() const {
  core::AudioEngineStats result;
  result.callbackCount = callbackCount_.load(std::memory_order_relaxed);
  result.lastError = lastError_;
  if (droppedRequests_.load() > 0 && result.lastError.empty()) {
    result.lastError = "an audio request could not be queued (message loop unavailable or out of memory)";
  }

  if (juce::AudioIODevice* device = deviceManager_.getCurrentAudioDevice()) {
    result.deviceOpen = device->isOpen();
    result.apiName = deviceManager_.getCurrentAudioDeviceType().toStdString();
    result.deviceName = device->getName().toStdString();
    result.sampleRate = device->getCurrentSampleRate();
    result.bufferSize = device->getCurrentBufferSizeSamples();
    result.outputChannels = device->getActiveOutputChannels().countNumberOfSetBits();
    if (result.sampleRate > 0.0) {
      result.outputLatencyMs = 1000.0 * device->getOutputLatencyInSamples() / result.sampleRate;
    }
    result.xrunCount = device->getXRunCount();
    result.cpuLoad = deviceManager_.getCpuUsage();
  }
  return result;
}

// RT
void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData,
                                                   int numOutputChannels, int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&) {
  juce::ScopedNoDenormals noDenormals;
  callbackCount_.fetch_add(1, std::memory_order_relaxed);
  stage_.render(outputChannelData, numOutputChannels, numSamples);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
  stage_.prepare(device != nullptr ? device->getCurrentSampleRate() : 0.0);
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceError(const juce::String& errorMessage) {
  // May be called from the device's thread while JUCE holds its lock: only queue the message, never throw or block.
  try {
    (void)postToMessageThread([this, text = errorMessage.toStdString()] { lastError_ = text; });
  } catch (...) {
    droppedRequests_.fetch_add(1);  // copying the text failed (out of memory)
  }
}

}  // namespace zyron::audio
