// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Engine/AudioEngine.hpp"

#include <algorithm>
#include <functional>
#include <variant>

#include "Audio/Decoder/JuceAudioFileDecoder.hpp"

namespace zyron::audio {

// The generator's clamping ranges and the Command API's validation ranges are two views of the same limits.
static_assert(TestToneGenerator::kMinFrequencyHz == core::limits::kToneFrequencyMinHz);
static_assert(TestToneGenerator::kMaxFrequencyHz == core::limits::kToneFrequencyMaxHz);
static_assert(TestToneGenerator::kMinLevelDb == core::limits::kToneLevelMinDb);
static_assert(TestToneGenerator::kMaxLevelDb == core::limits::kToneLevelMaxDb);

AudioEngine::AudioEngine() : AudioEngine(Services{}) {}

AudioEngine::AudioEngine(Services services)
    : services_(std::move(services)),
      graph_(std::make_unique<AudioGraph>()),
      loader_(std::make_unique<TrackLoader>()),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
  loader_->setFallbackDecoder(decodeWithJuce);  // MP3, FLAC, Ogg, AIFF and the WAV variants the built-in reader skips
  loader_->setClock(&callbackCount_);
}

AudioEngine::~AudioEngine() {
  JUCE_ASSERT_MESSAGE_THREAD  // alive_ is only sound if queued work and destruction share the message thread
      alive_->store(false);   // queued message-thread work becomes a no-op
  stop();
  loader_.reset();  // joins the worker: no load callback can touch this object afterwards
}

void AudioEngine::start(const core::AudioOutputSettings& requested) {
  JUCE_ASSERT_MESSAGE_THREAD
  gate_.setSuspended(false);
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
  gate_.setSuspended(true);
  if (deviceManager_.getCurrentAudioDevice() == nullptr) {
    return;  // nothing is calling us, so nothing will ever publish silence
  }
  const juce::uint32 deadline = juce::Time::getMillisecondCounter() + kFadeOutTimeoutMs;
  while (!gate_.isClosed() && juce::Time::getMillisecondCounter() < deadline) {
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
    return;
  }
  if (const auto* split = std::get_if<core::SeparateStems>(&command)) {
    const auto status = loadStatus(split->deck);
    if (status.phase != core::DeckLoadPhase::Ready) {
      postNotice("Stems: wait until the track has finished loading");
    } else if (!services_.separateStems) {
      postNotice("Stems: the neural network is not available in this build");
    } else if (status.stemPhase == core::StemPhase::Queued || status.stemPhase == core::StemPhase::Running) {
      postNotice("Stems: already being separated");
    } else {
      try {
        services_.separateStems(split->deck, status.track);
      } catch (...) {
        droppedRequests_.fetch_add(1);
      }
    }
    return;
  }
  if (const auto* sync = std::get_if<core::Sync>(&command)) {
    performSync(sync->deck);
    return;
  }
  if (const auto* recording = std::get_if<core::SetRecording>(&command)) {
    (void)postToMessageThread([this, enabled = recording->enabled] {
      if (services_.setRecording) {
        lastError_ = services_.setRecording(enabled);
      }
    });
    return;
  }
  if (const auto* load = std::get_if<core::LoadTrack>(&command)) {
    startLoad(*load);
    return;
  }
  if (const auto* unload = std::get_if<core::UnloadTrack>(&command)) {
    startUnload(*unload);
    (void)graph_->bridge().pushCommand(command);  // also pauses the deck on the audio thread
    return;
  }
  // Every other command is a realtime message. The bus serialises its sinks, so this is the single producer the
  // queue requires. A full queue is counted (droppedMessages in the live state), never hidden.
  (void)graph_->bridge().pushCommand(command);
}

void AudioEngine::startLoad(const core::LoadTrack& command) noexcept {
  try {
    std::optional<std::filesystem::path> path;
    if (services_.resolvePath) {
      path = services_.resolvePath(command.track);
    }
    if (!path.has_value()) {
      setLoadStatus(command.deck, core::DeckLoadStatus{core::DeckLoadPhase::Failed, command.track, 0,
                                                       "The track is not in the library", nullptr});
      return;
    }

    std::uint64_t requestId = 0;
    {
      std::lock_guard<std::mutex> lock(loadMutex_);
      requestId = ++latestRequest_[core::index(command.deck)];
    }
    if (graph_->deck(command.deck).isPlaying()) {
      // Fade the old track out through the realtime queue (this thread is its single producer) instead of swapping
      // the buffer under a playing deck.
      (void)graph_->bridge().pushCommand(core::Command{core::Pause{command.deck}});
    }
    setLoadStatus(command.deck, core::DeckLoadStatus{core::DeckLoadPhase::Loading, command.track, 0, {}, nullptr});

    loader_->loadTrackAsync(
        command.deck, *path, graph_->deck(command.deck),
        [this, deck = command.deck, requestId, track = command.track](const TrackLoadResult& result) {
          finishLoad(deck, requestId, track, result);
        });
  } catch (...) {
    droppedRequests_.fetch_add(1);  // out of memory: the load is lost, the audio thread is untouched
  }
}

void AudioEngine::postNotice(std::string text) {
  std::lock_guard<std::mutex> lock(loadMutex_);
  notice_ = std::move(text);
  ++noticeSerial_;
}

void AudioEngine::performSync(core::DeckId target) noexcept {
  try {
    constexpr const char* kNames = "ABCD";
    // Pick up the grids as the library knows them now: analysis may have finished after the track was loaded.
    std::array<bool, core::kDeckCount> hasGrid{};
    for (std::size_t i = 0; i < core::kDeckCount; ++i) {
      const auto id = static_cast<core::DeckId>(i);
      const auto track = graph_->deck(id).currentTrack();
      const auto status = loadStatus(id);
      if (track == nullptr || status.phase != core::DeckLoadPhase::Ready || !services_.gridFor) {
        continue;
      }
      const auto grid = services_.gridFor(status.track);
      if (!grid.has_value() || grid->bpm <= 0.0) {
        continue;
      }
      DeckGrid deckGrid;
      deckGrid.bpm = grid->bpm;
      deckGrid.sampleRate = static_cast<int>(track->sampleRate());
      deckGrid.firstBeatFrame = static_cast<std::int64_t>(grid->firstBeatSec * track->sampleRate());
      graph_->syncManager().setDeckGrid(id, deckGrid);
      hasGrid[i] = true;
    }

    if (!hasGrid[core::index(target)]) {
      postNotice(std::string("Sync: deck ") + kNames[core::index(target)] +
                 " has no beat grid (analysis is still running or found no beat)");
      return;
    }

    // Master: a playing deck with a grid if there is one, otherwise any other deck with a grid.
    std::optional<core::DeckId> master;
    for (const bool wantPlaying : {true, false}) {
      for (std::size_t i = 0; i < core::kDeckCount && !master.has_value(); ++i) {
        const auto id = static_cast<core::DeckId>(i);
        if (id != target && hasGrid[i] && (!wantPlaying || graph_->deck(id).isPlaying())) {
          master = id;
        }
      }
    }
    if (!master.has_value()) {
      postNotice("Sync: load a second analysed track to sync to");
      return;
    }
    graph_->syncManager().syncDeck(graph_->deck(target), target, graph_->deck(*master), *master);
    // A paused deck is aligned again on the audio thread when it starts: a seek queued before this sync, or the time
    // until the Play arrives, would otherwise leave the beats apart.
    if (!graph_->deck(target).isPlaying()) {
      const DeckGrid& tg = graph_->syncManager().getDeckGrid(target);
      const DeckGrid& mg = graph_->syncManager().getDeckGrid(*master);
      (void)graph_->bridge().pushMessage(RtMessage::makePhaseLock(target, *master, tg.bpm, tg.firstBeatFrame,
                                                                  tg.sampleRate, mg.bpm, mg.firstBeatFrame,
                                                                  mg.sampleRate));
    }
  } catch (...) {
    droppedRequests_.fetch_add(1);
  }
}

void AudioEngine::startUnload(const core::UnloadTrack& command) noexcept {
  try {
    {
      std::lock_guard<std::mutex> lock(loadMutex_);
      ++latestRequest_[core::index(command.deck)];  // an in-flight load no longer owns the status
    }
    loader_->unloadTrack(command.deck, graph_->deck(command.deck));
    setLoadStatus(command.deck, core::DeckLoadStatus{});
  } catch (...) {
    droppedRequests_.fetch_add(1);
  }
}

void AudioEngine::finishLoad(core::DeckId deck, std::uint64_t requestId, core::TrackId track,
                             const TrackLoadResult& result) {
  {
    std::lock_guard<std::mutex> lock(loadMutex_);
    if (latestRequest_[core::index(deck)] != requestId) {
      return;  // a newer request (or an unload) superseded this one
    }
  }
  core::DeckLoadStatus status;
  status.track = track;
  if (!result.success || result.buffer == nullptr) {
    status.phase = core::DeckLoadPhase::Failed;
    status.message = result.errorMessage.empty() ? "The file could not be decoded" : result.errorMessage;
    setLoadStatus(deck, std::move(status));
    return;
  }
  status.phase = core::DeckLoadPhase::Ready;
  if (services_.makeWaveform) {
    try {
      status.waveform = services_.makeWaveform(*result.buffer);
    } catch (...) {
      status.waveform = nullptr;  // the deck still plays; only the picture is missing
    }
  }
  setLoadStatus(deck, std::move(status));
  if (services_.trackReady) {
    try {
      services_.trackReady(deck, track);
    } catch (...) {
      droppedRequests_.fetch_add(1);
    }
  }
}

void AudioEngine::setStemStatus(core::DeckId deck, core::TrackId track, core::StemPhase phase, float progress,
                                std::string message) {
  std::lock_guard<std::mutex> lock(loadMutex_);
  core::DeckLoadStatus& status = loadStatus_[core::index(deck)];
  if (status.track != track || status.phase != core::DeckLoadPhase::Ready) {
    return;  // another track was loaded meanwhile
  }
  status.stemPhase = phase;
  status.stemProgress = progress;
  status.stemMessage = std::move(message);
  status.generation = ++statusGeneration_;
}

bool AudioEngine::attachStems(core::DeckId deck, core::TrackId track,
                              const std::shared_ptr<const TrackBuffer>& reference,
                              std::array<std::shared_ptr<const TrackBuffer>, core::kStemKindCount> stems) {
  {
    std::lock_guard<std::mutex> lock(loadMutex_);
    const core::DeckLoadStatus& status = loadStatus_[core::index(deck)];
    if (status.track != track || status.phase != core::DeckLoadPhase::Ready) {
      return false;
    }
  }
  DeckPlayer& player = graph_->deck(deck);
  auto previous = player.loadStemsFor(reference.get(), std::move(stems));
  if (!previous.has_value()) {
    return false;  // the deck switched to another track while the stems were being made
  }
  for (const auto& old : *previous) {
    if (old != nullptr) {
      loader_->retire(old);  // the audio thread may still be inside a block that reads it
    }
  }
  setStemStatus(deck, track, core::StemPhase::Ready, 1.0F, {});
  return true;
}

void AudioEngine::setLoadStatus(core::DeckId deck, core::DeckLoadStatus status) {
  std::lock_guard<std::mutex> lock(loadMutex_);
  status.generation = ++statusGeneration_;
  loadStatus_[core::index(deck)] = std::move(status);
}

core::DeckLoadStatus AudioEngine::loadStatus(core::DeckId deck) const {
  std::lock_guard<std::mutex> lock(loadMutex_);
  return loadStatus_[core::index(deck)];
}

core::LiveEngineState AudioEngine::liveState() {
  AudioTelemetry telemetry;
  (void)graph_->bridge().readTelemetry(telemetry);  // keeps the previous snapshot when nothing new was published
  core::LiveEngineState state;
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    const DeckTelemetry& src = telemetry.decks[i];
    core::LiveDeckState& dst = state.decks[i];
    dst.hasTrack = src.trackDurationSeconds > 0.0;
    dst.isPlaying = src.isPlaying;
    dst.positionSec = src.playheadSeconds;
    dst.durationSec = src.trackDurationSeconds;
    dst.peakLeft = src.peakLeft;
    dst.peakRight = src.peakRight;
    dst.hasStems = src.hasStems;
  }
  state.masterPeakLeft = telemetry.masterPeakLeft;
  state.masterPeakRight = telemetry.masterPeakRight;
  state.droppedMessages = telemetry.droppedRtMessages;
  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    state.decks[i].playbackSpeed = telemetry.decks[i].playbackSpeed;
  }
  {
    std::lock_guard<std::mutex> lock(loadMutex_);
    state.notice = notice_;
    state.noticeSerial = noticeSerial_;
  }
  return state;
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
  gate_.setSuspended(false);  // prepare() restarted the gate at gain 0, so the output fades in
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
  if (outputChannelData == nullptr || numOutputChannels <= 0 || numSamples <= 0) {
    return;
  }
  graph_->render(outputChannelData, numOutputChannels, numSamples);
  mixTestTone(outputChannelData, numOutputChannels, numSamples);
  gate_.process(outputChannelData, numOutputChannels, numSamples);
  // Last line of defence: never hand a NaN or an over-range sample to the device.
  for (int ch = 0; ch < numOutputChannels; ++ch) {
    float* out = outputChannelData[ch];
    if (out == nullptr) {
      continue;
    }
    for (int i = 0; i < numSamples; ++i) {
      const float v = out[i];
      out[i] = (v != v) ? 0.0F : std::clamp(v, -1.0F, 1.0F);
    }
  }
}

// RT
void AudioEngine::mixTestTone(float* const* outputs, int numChannels, int numSamples) noexcept {
  // Nothing to add while the tone is off and has faded out; the generator keeps its phase either way.
  if (!stage_.tone().enabled() && stage_.tone().isFadedOut()) {
    return;
  }
  const int toneChannels = std::min(numChannels, 2);  // the tone belongs on the master pair
  for (int offset = 0; offset < numSamples; offset += kToneBlock) {
    const int block = std::min(kToneBlock, numSamples - offset);
    stage_.tone().render(toneScratch_.data(), block);
    for (int ch = 0; ch < toneChannels; ++ch) {
      if (outputs[ch] == nullptr) {
        continue;
      }
      float* const out = outputs[ch] + offset;
      for (int i = 0; i < block; ++i) {
        out[i] += toneScratch_[static_cast<std::size_t>(i)];
      }
    }
  }
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
  const double sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
  stage_.prepare(sampleRate);
  gate_.prepare(sampleRate);
  graph_->prepare(sampleRate);
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
