// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Engine/AudioGraph.hpp"

#include <algorithm>

namespace zyron::audio {

AudioGraph::AudioGraph() {
  prepare(48000.0);
}

void AudioGraph::prepare(double sampleRate) noexcept {
  sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

  for (auto& d : decks_) {
    d.prepare(sampleRate_);
  }
  for (auto& c : channels_) {
    c.prepare(sampleRate_);
  }
  mixer_.prepare(sampleRate_);
  cueRouter_.prepare(sampleRate_);

  reset();
}

void AudioGraph::reset() noexcept {
  for (auto& buf : deckScratch_) {
    buf.fill(0.0F);
  }
  masterL_.fill(0.0F);
  masterR_.fill(0.0F);
  cueL_.fill(0.0F);
  cueR_.fill(0.0F);

  mixer_.reset();
  bridge_.reset();
}

void AudioGraph::render(float* const* outputs, int numChannels, int numSamples) noexcept {
  if (outputs == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  for (int offset = 0; offset < numSamples; offset += kMaxBlockSize) {
    const int blockSize = std::min(kMaxBlockSize, numSamples - offset);

    // 1. Process control/UI thread commands
    drainMessages();

    // 2. Render each of the 4 decks into scratch buffers, then process channel strips
    for (std::size_t i = 0; i < core::kDeckCount; ++i) {
      float* deckPtrs[2] = {deckScratch_[2 * i].data(), deckScratch_[2 * i + 1].data()};
      decks_[i].render(deckPtrs, 2, blockSize);
      channels_[i].process(deckPtrs, 2, blockSize);
    }

    // 3. Prepare inputs for 4-channel mixer
    const float* inL[4] = {deckScratch_[0].data(), deckScratch_[2].data(), deckScratch_[4].data(),
                           deckScratch_[6].data()};
    const float* inR[4] = {deckScratch_[1].data(), deckScratch_[3].data(), deckScratch_[5].data(),
                           deckScratch_[7].data()};

    mixer_.process(inL, inR, static_cast<int>(core::kDeckCount), masterL_.data(), masterR_.data(), blockSize);
    mixer_.processCue(inL, inR, static_cast<int>(core::kDeckCount), cueL_.data(), cueR_.data(), blockSize);

    // 4. Route to physical output channels via CueRouter
    float* chunkOutputs[4] = {nullptr, nullptr, nullptr, nullptr};
    const int outChans = std::min(numChannels, 4);
    for (int ch = 0; ch < outChans; ++ch) {
      if (outputs[ch] != nullptr) {
        chunkOutputs[ch] = outputs[ch] + offset;
      }
    }
    cueRouter_.route(masterL_.data(), masterR_.data(), cueL_.data(), cueR_.data(), chunkOutputs, numChannels, blockSize);

    // 5. Master recording tap
    auto* tap = masterTap_.load(std::memory_order_relaxed);
    if (tap != nullptr) {
      const float* tapPtrs[2] = {masterL_.data(), masterR_.data()};
      tap->writeSamples(tapPtrs, 2, blockSize);
    }
  }

  // 6. Publish telemetry
  updateTelemetry();
}

void AudioGraph::drainMessages() noexcept {
  RtMessage msg;
  while (bridge_.popMessage(msg)) {
    const auto deckIdx = core::index(msg.deck);
    switch (msg.type) {
      case RtMessageType::DeckPlay:
        if (deckIdx < core::kDeckCount) decks_[deckIdx].play();
        break;
      case RtMessageType::DeckPause:
        if (deckIdx < core::kDeckCount) decks_[deckIdx].pause();
        break;
      case RtMessageType::DeckCue:
        if (deckIdx < core::kDeckCount) decks_[deckIdx].cue();
        break;
      case RtMessageType::DeckSeek:
        if (deckIdx < core::kDeckCount) decks_[deckIdx].seek(msg.data.seekSample);
        break;
      case RtMessageType::DeckGain:
        if (deckIdx < core::kDeckCount) channels_[deckIdx].setGainDb(msg.data.gainDb);
        break;
      case RtMessageType::DeckVolume:
        if (deckIdx < core::kDeckCount) channels_[deckIdx].setVolume(msg.data.volumeLinear);
        break;
      case RtMessageType::DeckEq:
        if (deckIdx < core::kDeckCount) channels_[deckIdx].setEqDb(msg.data.eq.band, msg.data.eq.gainDb);
        break;
      case RtMessageType::DeckFilter:
        if (deckIdx < core::kDeckCount) channels_[deckIdx].setFilter(msg.data.filterBipolar);
        break;
      case RtMessageType::DeckPlaybackSpeed:
        if (deckIdx < core::kDeckCount) decks_[deckIdx].setPlaybackSpeed(msg.data.speedRatio);
        break;
      case RtMessageType::DeckStemVolume:
        if (deckIdx < core::kDeckCount) {
          decks_[deckIdx].stemMixer().setVolume(msg.data.stemControl.stem, msg.data.stemControl.volumeLinear);
        }
        break;
      case RtMessageType::DeckStemMute:
        if (deckIdx < core::kDeckCount) {
          decks_[deckIdx].stemMixer().setMute(msg.data.stemControl.stem, msg.data.stemControl.active);
        }
        break;
      case RtMessageType::DeckStemSolo:
        if (deckIdx < core::kDeckCount) {
          decks_[deckIdx].stemMixer().setSolo(msg.data.stemControl.stem, msg.data.stemControl.active);
        }
        break;
      case RtMessageType::DeckStemCue:
        if (deckIdx < core::kDeckCount) {
          decks_[deckIdx].stemMixer().setCue(msg.data.stemControl.stem, msg.data.stemControl.active);
        }
        break;
      case RtMessageType::MixerCrossfader:
        mixer_.setCrossfader(msg.data.crossfaderPosition);
        break;
      case RtMessageType::MixerCrossfaderCurve:
        mixer_.setCrossfaderCurve(msg.data.crossfaderCurve);
        break;
      case RtMessageType::MixerChannelAssign:
        mixer_.setChannelAssign(msg.data.channelAssign.channel, msg.data.channelAssign.assign);
        break;
      case RtMessageType::MixerMasterGain:
        mixer_.setMasterGainDb(msg.data.masterGainDb);
        break;
      case RtMessageType::MixerLimiter:
        mixer_.masterLimiter().setCeilingDb(msg.data.limiter.ceilingDb);
        break;
      case RtMessageType::MixerCue:
        mixer_.setCue(msg.data.cue.channel, msg.data.cue.enabled);
        break;
      case RtMessageType::TestTone:
      case RtMessageType::None:
      default:
        break;
    }
  }
}

void AudioGraph::updateTelemetry() noexcept {
  callbackCount_.fetch_add(1, std::memory_order_relaxed);

  AudioTelemetry telem{};
  telem.callbackCount = callbackCount_.load(std::memory_order_relaxed);
  telem.droppedRtMessages = bridge_.droppedMessagesCount();
  telem.masterPeakLeft = mixer_.masterPeakLeft();
  telem.masterPeakRight = mixer_.masterPeakRight();
  telem.limiterGainReduction = mixer_.masterLimiter().currentGainReduction();

  for (std::size_t i = 0; i < core::kDeckCount; ++i) {
    auto& dt = telem.decks[i];
    dt.playheadSample = decks_[i].currentFrame();
    dt.playheadSeconds = decks_[i].currentTimeSec();
    dt.trackDurationSeconds = decks_[i].durationSec();
    dt.isPlaying = decks_[i].isPlaying();
    dt.isCueing = false;
    dt.peakLeft = channels_[i].peakLeft();
    dt.peakRight = channels_[i].peakRight();
    dt.hasStems = decks_[i].hasStems();
  }

  bridge_.publishTelemetry(telem);
}

}  // namespace zyron::audio
