// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Deck/DeckPlayer.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::audio {

DeckPlayer::DeckPlayer() {
  prepare(48000.0);
}

void DeckPlayer::prepare(double deviceSampleRate) noexcept {
  deviceSampleRate_ = (deviceSampleRate > 0.0) ? deviceSampleRate : 48000.0;
  // 5 ms time constant: alpha = 1 - exp(-1 / (sampleRate * 0.005))
  rampCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(deviceSampleRate_) * 0.005F));
  if (rampCoeff_ <= 0.0F || rampCoeff_ > 1.0F) {
    rampCoeff_ = 0.005F;
  }

  stemMixer_.prepare(deviceSampleRate_);

  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    stemScratchL_[s].assign(8192, 0.0F);
    stemScratchR_[s].assign(8192, 0.0F);
  }
  stemMixMasterL_.assign(8192, 0.0F);
  stemMixMasterR_.assign(8192, 0.0F);
}

void DeckPlayer::loadTrack(std::shared_ptr<const TrackBuffer> track) noexcept {
  retainedTrack_ = std::move(track);
  activeBuffer_.store(retainedTrack_.get(), std::memory_order_release);
  playhead_.store(0.0, std::memory_order_relaxed);
  cueFrame_.store(0, std::memory_order_relaxed);
  playing_.store(false, std::memory_order_relaxed);
  targetPlaying_.store(false, std::memory_order_relaxed);
  playRamp_ = 0.0F;
}

void DeckPlayer::unloadTrack() noexcept {
  targetPlaying_.store(false, std::memory_order_relaxed);
  playing_.store(false, std::memory_order_relaxed);
  activeBuffer_.store(nullptr, std::memory_order_release);
  retainedTrack_.reset();
  unloadStems();
}

void DeckPlayer::loadStems(std::array<std::shared_ptr<const TrackBuffer>, core::kStemKindCount> stems) noexcept {
  retainedStems_ = std::move(stems);
  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    activeStemBuffers_[s].store(retainedStems_[s].get(), std::memory_order_release);
  }
  hasStems_.store(true, std::memory_order_release);

  if (activeBuffer_.load(std::memory_order_relaxed) == nullptr && retainedStems_[0] != nullptr) {
    playhead_.store(0.0, std::memory_order_relaxed);
    cueFrame_.store(0, std::memory_order_relaxed);
    playing_.store(false, std::memory_order_relaxed);
    targetPlaying_.store(false, std::memory_order_relaxed);
    playRamp_ = 0.0F;
  }
}

void DeckPlayer::unloadStems() noexcept {
  hasStems_.store(false, std::memory_order_release);
  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    activeStemBuffers_[s].store(nullptr, std::memory_order_release);
    retainedStems_[s].reset();
  }
}

bool DeckPlayer::hasStems() const noexcept {
  return hasStems_.load(std::memory_order_acquire);
}

void DeckPlayer::play() noexcept {
  if (activeBuffer_.load(std::memory_order_relaxed) != nullptr || hasStems_.load(std::memory_order_relaxed)) {
    targetPlaying_.store(true, std::memory_order_relaxed);
    playing_.store(true, std::memory_order_relaxed);
  }
}

void DeckPlayer::pause() noexcept {
  targetPlaying_.store(false, std::memory_order_relaxed);
}

void DeckPlayer::cue() noexcept {
  if (targetPlaying_.load(std::memory_order_relaxed)) {
    targetPlaying_.store(false, std::memory_order_relaxed);
    playing_.store(false, std::memory_order_relaxed);
    playRamp_ = 0.0F;
    playhead_.store(static_cast<double>(cueFrame_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
  } else {
    const std::int64_t cur = static_cast<std::int64_t>(playhead_.load(std::memory_order_relaxed));
    cueFrame_.store(cur, std::memory_order_relaxed);
  }
}

void DeckPlayer::seek(std::int64_t frame) noexcept {
  const auto* buffer = activeBuffer_.load(std::memory_order_relaxed);
  if (buffer == nullptr && hasStems_.load(std::memory_order_relaxed)) {
    buffer = activeStemBuffers_[0].load(std::memory_order_relaxed);
  }

  if (buffer != nullptr) {
    const std::int64_t maxFrame = buffer->numFrames();
    frame = std::clamp<std::int64_t>(frame, 0, maxFrame);
  } else {
    frame = 0;
  }
  playhead_.store(static_cast<double>(frame), std::memory_order_relaxed);
}

void DeckPlayer::seekSeconds(double seconds) noexcept {
  const auto* buffer = activeBuffer_.load(std::memory_order_relaxed);
  if (buffer == nullptr && hasStems_.load(std::memory_order_relaxed)) {
    buffer = activeStemBuffers_[0].load(std::memory_order_relaxed);
  }

  if (buffer != nullptr && buffer->sampleRate() > 0.0) {
    const auto frame = static_cast<std::int64_t>(std::max(0.0, seconds * buffer->sampleRate()));
    seek(frame);
  }
}

void DeckPlayer::setPlaybackSpeed(double speed) noexcept {
  speed = std::clamp(speed, 0.0, 4.0);
  speed_.store(speed, std::memory_order_relaxed);
}

void DeckPlayer::setGainDb(float gainDb) noexcept {
  gainDb = std::clamp(gainDb, -24.0F, 12.0F);
  targetGainDb_.store(gainDb, std::memory_order_relaxed);
}

void DeckPlayer::setVolume(float linearVolume) noexcept {
  linearVolume = std::clamp(linearVolume, 0.0F, 1.0F);
  targetVolume_.store(linearVolume, std::memory_order_relaxed);
}

void DeckPlayer::render(float* const* outputChannels, int numChannels, int numSamples) noexcept {
  if (outputChannels == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  // Always write silence to all channels first
  for (int ch = 0; ch < numChannels; ++ch) {
    if (outputChannels[ch] != nullptr) {
      std::fill_n(outputChannels[ch], numSamples, 0.0F);
    }
  }

  const bool targetPlay = targetPlaying_.load(std::memory_order_relaxed);
  if (!playing_.load(std::memory_order_relaxed) && playRamp_ <= 1e-5F) {
    return;
  }

  const bool stemsMode = hasStems_.load(std::memory_order_acquire);
  const auto* masterBuffer = activeBuffer_.load(std::memory_order_acquire);
  const auto* refBuffer = stemsMode ? activeStemBuffers_[0].load(std::memory_order_acquire) : masterBuffer;

  if (refBuffer == nullptr) {
    return;
  }

  const double spd = speed_.load(std::memory_order_relaxed);
  const double step = spd * (refBuffer->sampleRate() / deviceSampleRate_);
  const float targetGainLinear = std::pow(10.0F, targetGainDb_.load(std::memory_order_relaxed) * 0.05F);
  const float targetVol = targetVolume_.load(std::memory_order_relaxed);
  double head = playhead_.load(std::memory_order_relaxed);
  const std::int64_t totalFrames = refBuffer->numFrames();

  if (stemsMode) {
    // Stem Playback: render 4 synchronized stems through StemMixer
    std::array<const TrackBuffer*, core::kStemKindCount> stemBufs{};
    for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
      stemBufs[s] = activeStemBuffers_[s].load(std::memory_order_relaxed);
    }

    const int framesToProcess = std::min(numSamples, static_cast<int>(stemMixMasterL_.size()));

    for (int i = 0; i < framesToProcess; ++i) {
      playRamp_ += rampCoeff_ * ((targetPlay ? 1.0F : 0.0F) - playRamp_);
      if (playRamp_ < 1e-4F && !targetPlay) {
        playing_.store(false, std::memory_order_relaxed);
        playRamp_ = 0.0F;
        break;
      }

      currentGainLinear_ += rampCoeff_ * (targetGainLinear - currentGainLinear_);
      currentVolume_ += rampCoeff_ * (targetVol - currentVolume_);

      if (loopActive_.load(std::memory_order_relaxed)) {
        const double lStart = static_cast<double>(loopStartFrame_.load(std::memory_order_relaxed));
        const double lEnd = static_cast<double>(loopEndFrame_.load(std::memory_order_relaxed));
        if (lEnd > lStart && head >= lEnd) {
          head = lStart + std::fmod(head - lStart, lEnd - lStart);
        }
      }

      if (head >= static_cast<double>(totalFrames)) {
        playing_.store(false, std::memory_order_relaxed);
        targetPlaying_.store(false, std::memory_order_relaxed);
        playRamp_ = 0.0F;
        break;
      }

      const auto f0 = static_cast<std::int64_t>(head);
      const auto f1 = f0 + 1;
      const float frac = static_cast<float>(head - static_cast<double>(f0));

      for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
        const auto* buf = stemBufs[s];
        if (buf != nullptr) {
          const int chCount = buf->numChannels();
          const float s0L = buf->sampleAt(0, f0);
          const float s1L = buf->sampleAt(0, f1);
          stemScratchL_[s][static_cast<std::size_t>(i)] = s0L + frac * (s1L - s0L);

          const int rCh = (chCount > 1) ? 1 : 0;
          const float s0R = buf->sampleAt(rCh, f0);
          const float s1R = buf->sampleAt(rCh, f1);
          stemScratchR_[s][static_cast<std::size_t>(i)] = s0R + frac * (s1R - s0R);
        } else {
          stemScratchL_[s][static_cast<std::size_t>(i)] = 0.0F;
          stemScratchR_[s][static_cast<std::size_t>(i)] = 0.0F;
        }
      }

      head += step;
    }

    const float* inL[core::kStemKindCount] = {stemScratchL_[0].data(), stemScratchL_[1].data(),
                                             stemScratchL_[2].data(), stemScratchL_[3].data()};
    const float* inR[core::kStemKindCount] = {stemScratchR_[0].data(), stemScratchR_[1].data(),
                                             stemScratchR_[2].data(), stemScratchR_[3].data()};

    stemMixer_.process(inL, inR, stemMixMasterL_.data(), stemMixMasterR_.data(), framesToProcess);

    const float overallAmp = playRamp_ * currentGainLinear_ * currentVolume_;
    for (int i = 0; i < framesToProcess; ++i) {
      if (numChannels > 0 && outputChannels[0] != nullptr) {
        outputChannels[0][i] = stemMixMasterL_[static_cast<std::size_t>(i)] * overallAmp;
      }
      if (numChannels > 1 && outputChannels[1] != nullptr) {
        outputChannels[1][i] = stemMixMasterR_[static_cast<std::size_t>(i)] * overallAmp;
      }
    }
  } else {
    // Master track playback
    const int trackChannels = masterBuffer->numChannels();

    for (int i = 0; i < numSamples; ++i) {
      playRamp_ += rampCoeff_ * ((targetPlay ? 1.0F : 0.0F) - playRamp_);
      if (playRamp_ < 1e-4F && !targetPlay) {
        playing_.store(false, std::memory_order_relaxed);
        playRamp_ = 0.0F;
        break;
      }

      currentGainLinear_ += rampCoeff_ * (targetGainLinear - currentGainLinear_);
      currentVolume_ += rampCoeff_ * (targetVol - currentVolume_);

      if (loopActive_.load(std::memory_order_relaxed)) {
        const double lStart = static_cast<double>(loopStartFrame_.load(std::memory_order_relaxed));
        const double lEnd = static_cast<double>(loopEndFrame_.load(std::memory_order_relaxed));
        if (lEnd > lStart && head >= lEnd) {
          head = lStart + std::fmod(head - lStart, lEnd - lStart);
        }
      }

      if (head >= static_cast<double>(totalFrames)) {
        playing_.store(false, std::memory_order_relaxed);
        targetPlaying_.store(false, std::memory_order_relaxed);
        playRamp_ = 0.0F;
        break;
      }

      const float amp = playRamp_ * currentGainLinear_ * currentVolume_;
      const auto f0 = static_cast<std::int64_t>(head);
      const auto f1 = f0 + 1;
      const float frac = static_cast<float>(head - static_cast<double>(f0));

      for (int ch = 0; ch < numChannels; ++ch) {
        if (outputChannels[ch] == nullptr) {
          continue;
        }
        const int trackCh = (trackChannels > 1) ? std::min(ch, trackChannels - 1) : 0;
        const float s0 = masterBuffer->sampleAt(trackCh, f0);
        const float s1 = masterBuffer->sampleAt(trackCh, f1);
        const float interpolated = s0 + frac * (s1 - s0);
        outputChannels[ch][i] = interpolated * amp;
      }

      head += step;
    }
  }

  playhead_.store(head, std::memory_order_relaxed);
}

void DeckPlayer::renderStems(float* const* outStemLefts, float* const* outStemRights, int numSamples) noexcept {
  if (outStemLefts == nullptr || outStemRights == nullptr || numSamples <= 0) {
    return;
  }

  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    if (outStemLefts[s] != nullptr) {
      std::fill_n(outStemLefts[s], numSamples, 0.0F);
    }
    if (outStemRights[s] != nullptr) {
      std::fill_n(outStemRights[s], numSamples, 0.0F);
    }
  }

  if (!hasStems_.load(std::memory_order_acquire)) {
    return;
  }

  const auto* refBuffer = activeStemBuffers_[0].load(std::memory_order_acquire);
  if (refBuffer == nullptr) {
    return;
  }

  const int framesToProcess = std::min(numSamples, static_cast<int>(stemMixMasterL_.size()));
  double head = playhead_.load(std::memory_order_relaxed);
  const double spd = speed_.load(std::memory_order_relaxed);
  const double step = spd * (refBuffer->sampleRate() / deviceSampleRate_);

  for (int i = 0; i < framesToProcess; ++i) {
    const auto f0 = static_cast<std::int64_t>(head);
    const auto f1 = f0 + 1;
    const float frac = static_cast<float>(head - static_cast<double>(f0));

    for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
      const auto* buf = activeStemBuffers_[s].load(std::memory_order_relaxed);
      if (buf != nullptr) {
        const int chCount = buf->numChannels();
        const float s0L = buf->sampleAt(0, f0);
        const float s1L = buf->sampleAt(0, f1);
        stemScratchL_[s][static_cast<std::size_t>(i)] = s0L + frac * (s1L - s0L);

        const int rCh = (chCount > 1) ? 1 : 0;
        const float s0R = buf->sampleAt(rCh, f0);
        const float s1R = buf->sampleAt(rCh, f1);
        stemScratchR_[s][static_cast<std::size_t>(i)] = s0R + frac * (s1R - s0R);
      } else {
        stemScratchL_[s][static_cast<std::size_t>(i)] = 0.0F;
        stemScratchR_[s][static_cast<std::size_t>(i)] = 0.0F;
      }
    }

    head += step;
  }

  const float* inL[core::kStemKindCount] = {stemScratchL_[0].data(), stemScratchL_[1].data(),
                                           stemScratchL_[2].data(), stemScratchL_[3].data()};
  const float* inR[core::kStemKindCount] = {stemScratchR_[0].data(), stemScratchR_[1].data(),
                                           stemScratchR_[2].data(), stemScratchR_[3].data()};

  stemMixer_.processStems(inL, inR, outStemLefts, outStemRights, framesToProcess);
}

bool DeckPlayer::hasTrack() const noexcept {
  return activeBuffer_.load(std::memory_order_relaxed) != nullptr || hasStems_.load(std::memory_order_relaxed);
}

std::shared_ptr<const TrackBuffer> DeckPlayer::currentTrack() const noexcept {
  return retainedTrack_;
}

bool DeckPlayer::isPlaying() const noexcept {
  return targetPlaying_.load(std::memory_order_relaxed) && playing_.load(std::memory_order_relaxed);
}

std::int64_t DeckPlayer::currentFrame() const noexcept {
  return static_cast<std::int64_t>(playhead_.load(std::memory_order_relaxed));
}

double DeckPlayer::currentTimeSec() const noexcept {
  const auto* buffer = activeBuffer_.load(std::memory_order_relaxed);
  if (buffer == nullptr && hasStems_.load(std::memory_order_relaxed)) {
    buffer = activeStemBuffers_[0].load(std::memory_order_relaxed);
  }
  if (buffer != nullptr && buffer->sampleRate() > 0.0) {
    return playhead_.load(std::memory_order_relaxed) / buffer->sampleRate();
  }
  return 0.0;
}

std::int64_t DeckPlayer::cueFrame() const noexcept {
  return cueFrame_.load(std::memory_order_relaxed);
}

double DeckPlayer::durationSec() const noexcept {
  const auto* buffer = activeBuffer_.load(std::memory_order_relaxed);
  if (buffer == nullptr && hasStems_.load(std::memory_order_relaxed)) {
    buffer = activeStemBuffers_[0].load(std::memory_order_relaxed);
  }
  return buffer != nullptr ? buffer->durationSec() : 0.0;
}

double DeckPlayer::playbackSpeed() const noexcept {
  return speed_.load(std::memory_order_relaxed);
}

void DeckPlayer::setLoop(std::int64_t startFrame, std::int64_t endFrame) noexcept {
  const auto s = std::max<std::int64_t>(0, startFrame);
  const auto e = std::max<std::int64_t>(s, endFrame);
  loopStartFrame_.store(s, std::memory_order_release);
  loopEndFrame_.store(e, std::memory_order_release);
}

void DeckPlayer::setLoopActive(bool active) noexcept {
  loopActive_.store(active, std::memory_order_release);
}

bool DeckPlayer::isLoopActive() const noexcept {
  return loopActive_.load(std::memory_order_acquire);
}

std::int64_t DeckPlayer::loopStartFrame() const noexcept {
  return loopStartFrame_.load(std::memory_order_relaxed);
}

std::int64_t DeckPlayer::loopEndFrame() const noexcept {
  return loopEndFrame_.load(std::memory_order_relaxed);
}

}  // namespace zyron::audio
