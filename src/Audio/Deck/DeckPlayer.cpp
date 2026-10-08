// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Deck/DeckPlayer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace zyron::audio {

namespace {
constexpr int kMaxJumpsPerBlock = 32;
constexpr int kStemReadChunk = 4096;       // frames per StemMixer call while reading the stems for the stretcher
constexpr double kShortLoopSeconds = 0.3;  // loops shorter than this are rolls: they play varispeed, not stretched
constexpr float kMaxKeyShiftSemitones = 12.0F;
constexpr double kBlendSeconds = 0.010;    // crossfade between the stretched and the untouched signal

/// Catmull-Rom interpolation of `data` at fractional position `p` (silence outside [0, length)).
float cubicAt(const float* data, std::int64_t length, double p) noexcept {
  const auto at = [&](std::int64_t index) noexcept { return (index >= 0 && index < length) ? data[index] : 0.0F; };
  const double whole = std::floor(p);
  const auto i = static_cast<std::int64_t>(whole);
  const auto f = static_cast<float>(p - whole);
  const float y0 = at(i - 1);
  const float y1 = at(i);
  const float y2 = at(i + 1);
  const float y3 = at(i + 2);
  const float a = 0.5F * (-y0 + 3.0F * y1 - 3.0F * y2 + y3);
  const float b = 0.5F * (2.0F * y0 - 5.0F * y1 + 4.0F * y2 - y3);
  const float c = 0.5F * (y2 - y0);
  return ((a * f + b) * f + c) * f + y1;
}

/// One channel of `buffer` read at `frames` positions position, position + step, ... (cubic interpolation, silence
/// outside the track, a mono track feeds every channel). Realtime safe.
void readResampled(const TrackBuffer* buffer, int channel, double position, double step, int frames,
                   float* destination) noexcept {
  if (buffer == nullptr || buffer->numChannels() <= 0) {
    std::fill_n(destination, frames, 0.0F);
    return;
  }
  const float* data = buffer->channelData(std::min(channel, buffer->numChannels() - 1));
  const std::int64_t length = buffer->numFrames();
  const auto at = [&](std::int64_t index) noexcept { return (index >= 0 && index < length) ? data[index] : 0.0F; };

  const double base = std::floor(position);
  if (step == 1.0 && position - base < 1.0e-9) {  // same rate, whole frames: a plain copy
    const auto first = static_cast<std::int64_t>(base);
    for (int j = 0; j < frames; ++j) {
      destination[j] = at(first + j);
    }
    return;
  }
  for (int j = 0; j < frames; ++j) {
    destination[j] = cubicAt(data, length, position + static_cast<double>(j) * step);
  }
}
}  // namespace

/// Every sample where the read position jumps (a seek, each loop wrap, the end of a scratch): each one is declicked.
struct DeckPlayer::JumpList {
  std::array<int, kMaxJumpsPerBlock> at{};
  int count{0};
  void add(int sample) noexcept {
    if (count < kMaxJumpsPerBlock && (count == 0 || at[static_cast<std::size_t>(count - 1)] != sample)) {
      at[static_cast<std::size_t>(count++)] = sample;
    }
  }
};

/// What the stretched path needs from render()'s block preamble.
struct DeckPlayer::BlockParams {
  bool targetPlay{false};
  double speed{1.0};    // source frames (device rate) per output frame
  double step{1.0};     // track frames per output frame
  double srcStep{1.0};  // track frames per source frame
  float targetGainLinear{1.0F};
  float targetVolume{1.0F};
  std::int64_t totalFrames{0};
  std::uint32_t epoch{0};
  bool blendable{false};  // the untouched signal can be read next to the stretched one (full mix only)
};

DeckPlayer::DeckPlayer() {
  prepare(48000.0);
}

void DeckPlayer::prepare(double deviceSampleRate) noexcept {
  deviceSampleRate_ = (deviceSampleRate > 0.0) ? deviceSampleRate : 48000.0;
  // 5 ms time constant: alpha = 1 - exp(-1 / (sampleRate * 0.005))
  rampCoeff_ = 1.0F - std::exp(-1.0F / (static_cast<float>(deviceSampleRate_) * 0.005F));
  declickDecay_ = std::exp(-1.0F / (static_cast<float>(deviceSampleRate_) * 0.003F));  // 3 ms
  scratchGateCoeff_ = 1.0 - std::exp(-1.0 / (deviceSampleRate_ * 0.001));  // 1 ms: a fader cut, not a click
  if (rampCoeff_ <= 0.0F || rampCoeff_ > 1.0F) {
    rampCoeff_ = 0.005F;
  }

  stemMixer_.prepare(deviceSampleRate_);
  keylock_.prepare(deviceSampleRate_);
  stretchPath_ = false;
  stretchExiting_ = false;
  stretchBlend_ = 0.0F;
  stretchBlendStep_ = static_cast<float>(1.0 / (kBlendSeconds * deviceSampleRate_));

  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    stemScratchL_[s].assign(8192, 0.0F);
    stemScratchR_[s].assign(8192, 0.0F);
  }
  stemAmp_.assign(8192, 0.0F);
  stemMixMasterL_.assign(8192, 0.0F);
  stemMixMasterR_.assign(8192, 0.0F);
}

DeckPlayer::Released DeckPlayer::loadTrack(std::shared_ptr<const TrackBuffer> track) noexcept {
  std::lock_guard<std::mutex> lock(controlMutex_);
  Released released;
  // Stems belong to the track they were separated from: they leave with it.
  hasStems_.store(false, std::memory_order_release);
  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    activeStemBuffers_[s].store(nullptr, std::memory_order_release);
    released.stems[s] = std::move(retainedStems_[s]);
  }
  released.track = std::exchange(retainedTrack_, std::move(track));
  activeBuffer_.store(retainedTrack_.get(), std::memory_order_release);
  playhead_.store(0.0, std::memory_order_relaxed);
  cueFrame_.store(0, std::memory_order_relaxed);
  playing_.store(false, std::memory_order_relaxed);
  targetPlaying_.store(false, std::memory_order_relaxed);
  rampResetRequested_.store(true, std::memory_order_release);
  positionEpoch_.fetch_add(1, std::memory_order_release);
  return released;
}

DeckPlayer::Released DeckPlayer::unloadTrack() noexcept {
  std::lock_guard<std::mutex> lock(controlMutex_);
  Released released;
  targetPlaying_.store(false, std::memory_order_relaxed);
  playing_.store(false, std::memory_order_relaxed);
  activeBuffer_.store(nullptr, std::memory_order_release);
  released.track = std::move(retainedTrack_);
  retainedTrack_.reset();
  hasStems_.store(false, std::memory_order_release);
  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    activeStemBuffers_[s].store(nullptr, std::memory_order_release);
    released.stems[s] = std::move(retainedStems_[s]);
  }
  positionEpoch_.fetch_add(1, std::memory_order_release);
  return released;
}

std::optional<DeckPlayer::StemBuffers> DeckPlayer::loadStemsFor(const TrackBuffer* expectedTrack,
                                                                 StemBuffers stems) noexcept {
  std::lock_guard<std::mutex> lock(controlMutex_);
  if (expectedTrack == nullptr || retainedTrack_.get() != expectedTrack) {
    return std::nullopt;
  }
  return installStemsLocked(std::move(stems));
}

DeckPlayer::StemBuffers DeckPlayer::loadStems(StemBuffers stems) noexcept {
  std::lock_guard<std::mutex> lock(controlMutex_);
  return installStemsLocked(std::move(stems));
}

DeckPlayer::StemBuffers DeckPlayer::installStemsLocked(StemBuffers stems) noexcept {
  StemBuffers previous = std::exchange(retainedStems_, std::move(stems));
  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    activeStemBuffers_[s].store(retainedStems_[s].get(), std::memory_order_release);
  }
  hasStems_.store(true, std::memory_order_release);

  if (activeBuffer_.load(std::memory_order_relaxed) == nullptr && retainedStems_[0] != nullptr) {
    playhead_.store(0.0, std::memory_order_relaxed);
    cueFrame_.store(0, std::memory_order_relaxed);
    playing_.store(false, std::memory_order_relaxed);
    targetPlaying_.store(false, std::memory_order_relaxed);
    rampResetRequested_.store(true, std::memory_order_release);
    positionEpoch_.fetch_add(1, std::memory_order_release);
  }
  return previous;
}

DeckPlayer::StemBuffers DeckPlayer::unloadStems() noexcept {
  std::lock_guard<std::mutex> lock(controlMutex_);
  StemBuffers previous;
  hasStems_.store(false, std::memory_order_release);
  for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
    activeStemBuffers_[s].store(nullptr, std::memory_order_release);
    previous[s] = std::move(retainedStems_[s]);
  }
  return previous;
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
    rampResetRequested_.store(true, std::memory_order_release);
    playhead_.store(static_cast<double>(cueFrame_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    positionEpoch_.fetch_add(1, std::memory_order_release);
    jumpPending_.store(true, std::memory_order_release);
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
  positionEpoch_.fetch_add(1, std::memory_order_release);
  jumpPending_.store(true, std::memory_order_release);
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
  speedSerial_.fetch_add(1, std::memory_order_release);
}

void DeckPlayer::glideSpeed(double target, double seconds) noexcept {
  const double now = speed_.load(std::memory_order_relaxed);
  const double samples = std::max(1.0, seconds * deviceSampleRate_);
  glide_.active = true;
  glide_.target = std::clamp(target, 0.0, 4.0);
  glide_.perSample = (glide_.target - now) / samples;
  glide_.serial = speedSerial_.load(std::memory_order_acquire);
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

  stretching_.store(false, std::memory_order_relaxed);  // set again below when this block goes through the stretcher

  // Requests from other threads that touch state only this thread may write.
  if (rampResetRequested_.exchange(false, std::memory_order_acq_rel)) {
    playRamp_ = 0.0F;
  }
  const std::uint32_t epoch = positionEpoch_.load(std::memory_order_acquire);
  bool jumped = jumpPending_.exchange(false, std::memory_order_acq_rel);

  const bool targetPlay = targetPlaying_.load(std::memory_order_relaxed);
  if (!playing_.load(std::memory_order_relaxed) && playRamp_ <= 1e-5F) {
    forgetStretch();
    releaseTail(outputChannels, numChannels, numSamples);
    return;
  }

  const bool stemsMode = hasStems_.load(std::memory_order_acquire);
  const auto* masterBuffer = activeBuffer_.load(std::memory_order_acquire);
  const auto* refBuffer = stemsMode ? activeStemBuffers_[0].load(std::memory_order_acquire) : masterBuffer;

  if (refBuffer == nullptr) {
    forgetStretch();
    releaseTail(outputChannels, numChannels, numSamples);
    return;
  }

  // A jump of the read position (seek, a switch between the full mix and the stems) would be a step in the output:
  // start from the last output value and let it fade into the new signal.
  if (stemsMode != lastStemsMode_) {
    lastStemsMode_ = stemsMode;
    jumped = true;
  }
  JumpList jumps;
  const auto addJump = [&](int at) noexcept { jumps.add(at); };
  if (jumped) {
    addJump(0);
  }
  // A seek, cue, load or stop while scratching ends the scratch where it is (no jump back to where it began).
  if (scratch_.active && (epoch != scratch_.epoch || !targetPlay)) {
    scratch_.active = false;
  }

  if (glide_.active) {
    if (speedSerial_.load(std::memory_order_acquire) != glide_.serial) {
      glide_.active = false;  // someone set the speed since: their value wins
    } else {
      const double now = speed_.load(std::memory_order_relaxed);
      double next = now + glide_.perSample * numSamples;  // a hair per block: far below what anyone can hear
      if ((glide_.perSample >= 0.0 && next >= glide_.target) || (glide_.perSample < 0.0 && next <= glide_.target)) {
        next = glide_.target;
        glide_.active = false;
      }
      speed_.store(next, std::memory_order_relaxed);
    }
  }
  const double spd = speed_.load(std::memory_order_relaxed);
  const double step = spd * (refBuffer->sampleRate() / deviceSampleRate_);
  const float targetGainLinear = std::pow(10.0F, targetGainDb_.load(std::memory_order_relaxed) * 0.05F);
  const float targetVol = targetVolume_.load(std::memory_order_relaxed);
  double head = playhead_.load(std::memory_order_relaxed);
  const std::int64_t totalFrames = refBuffer->numFrames();

  // Keylock / key shift: through the stretcher, or plain varispeed (which is also what an untouched 1.0x deck does).
  // The two signals have different phase (the stretcher drifts away from the source), so a plain switch would step:
  // the full mix crossfades for 10 ms instead (the stretcher is aligned to the playhead when it starts). The stems go
  // through a stateful mixer that cannot be fed twice, and a scratch must bite at once: those switch with the declick.
  bool stretch = wantsStretch(spd, refBuffer);
  const bool canBlend = !stemsMode && !scratch_.active;
  stretchExiting_ = false;
  if (stretchPath_ && !stretch && canBlend && stretchBlend_ < 1.0F) {
    stretch = true;  // keep rendering stretched while the untouched signal fades in
    stretchExiting_ = true;
  }
  if (stretch != stretchPath_) {
    const bool enteringBlended = stretch && canBlend;
    const bool leftBlended = !stretch && canBlend;  // the fade finished: both signals are the same by now
    stretchPath_ = stretch;
    stretchBlend_ = enteringBlended ? 1.0F : 0.0F;
    if (!enteringBlended && !leftBlended) {
      addJump(0);
    }
    keylock_.invalidate();
  }
  stretching_.store(stretch, std::memory_order_relaxed);

  if (stretch) {
    if (jumped || epoch != stretchEpoch_) {
      keylock_.invalidate();  // the playhead moved under us (seek, cue, load, switch to stems): restart at the new place
    }
    sourceStems_ = stemsMode;
    sourceMaster_ = masterBuffer;
    for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
      sourceStemBufs_[s] = activeStemBuffers_[s].load(std::memory_order_relaxed);
    }
    BlockParams block;
    block.targetPlay = targetPlay;
    block.speed = spd;
    block.step = step;
    block.srcStep = refBuffer->sampleRate() / deviceSampleRate_;
    block.targetGainLinear = targetGainLinear;
    block.targetVolume = targetVol;
    block.totalFrames = totalFrames;
    block.epoch = epoch;
    block.blendable = canBlend;
    renderStretched(outputChannels, numChannels, numSamples, block, jumps, head);
  } else if (stemsMode) {
    keylock_.invalidate();
    // Stem Playback: render 4 synchronized stems through StemMixer
    std::array<const TrackBuffer*, core::kStemKindCount> stemBufs{};
    for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
      stemBufs[s] = activeStemBuffers_[s].load(std::memory_order_relaxed);
    }

    const int framesToProcess = std::min(numSamples, static_cast<int>(stemMixMasterL_.size()));
    int produced = 0;  // samples that carry signal; a stop or the end of the track leaves the rest silent

    for (int i = 0; i < framesToProcess; ++i) {
      playRamp_ += rampCoeff_ * ((targetPlay ? 1.0F : 0.0F) - playRamp_);
      if (playRamp_ < 1e-4F && !targetPlay) {
        playing_.store(false, std::memory_order_relaxed);
        playRamp_ = 0.0F;
        break;
      }

      currentGainLinear_ += rampCoeff_ * (targetGainLinear - currentGainLinear_);
      currentVolume_ += rampCoeff_ * (targetVol - currentVolume_);

      // A scratch moves the record itself: speed and direction per sample, plus the fader cuts of the pattern.
      float scratchGain = 1.0F;
      double velocity = 1.0;
      if (scratch_.active && !advanceScratch(velocity, scratchGain)) {
        if (scratch_.pattern == static_cast<int>(core::ScratchPattern::Backspin) ||
            scratch_.pattern == static_cast<int>(core::ScratchPattern::Brake)) {
          playing_.store(false, std::memory_order_relaxed);  // a backspin ends with the record stopped
          targetPlaying_.store(false, std::memory_order_relaxed);
          playRamp_ = 0.0F;
          break;
        }
        head = scratch_.anchor + scratch_.length * step;  // carry on where the record would be without the scratch
        addJump(i);
      }

      if (loopActive_.load(std::memory_order_relaxed)) {
        const double lStart = static_cast<double>(loopStartFrame_.load(std::memory_order_relaxed));
        const double lEnd = static_cast<double>(loopEndFrame_.load(std::memory_order_relaxed));
        if (lEnd > lStart && head >= lEnd) {
          head = lStart + std::fmod(head - lStart, lEnd - lStart);
          addJump(i);
        }
      }

      if (head >= static_cast<double>(totalFrames)) {
        playing_.store(false, std::memory_order_relaxed);
        targetPlaying_.store(false, std::memory_order_relaxed);
        playRamp_ = 0.0F;
        break;
      }

      stemAmp_[static_cast<std::size_t>(i)] = playRamp_ * currentGainLinear_ * currentVolume_ * scratchGain;
      produced = i + 1;

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

      head = std::max(0.0, head + step * velocity);
    }

    const float* inL[core::kStemKindCount] = {stemScratchL_[0].data(), stemScratchL_[1].data(),
                                             stemScratchL_[2].data(), stemScratchL_[3].data()};
    const float* inR[core::kStemKindCount] = {stemScratchR_[0].data(), stemScratchR_[1].data(),
                                             stemScratchR_[2].data(), stemScratchR_[3].data()};

    stemMixer_.process(inL, inR, stemMixMasterL_.data(), stemMixMasterR_.data(), framesToProcess);

    for (int i = 0; i < produced; ++i) {
      const float amp = stemAmp_[static_cast<std::size_t>(i)];
      if (numChannels > 0 && outputChannels[0] != nullptr) {
        outputChannels[0][i] = stemMixMasterL_[static_cast<std::size_t>(i)] * amp;
      }
      if (numChannels > 1 && outputChannels[1] != nullptr) {
        outputChannels[1][i] = stemMixMasterR_[static_cast<std::size_t>(i)] * amp;
      }
    }
  } else {
    keylock_.invalidate();
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

      // A scratch moves the record itself: speed and direction per sample, plus the fader cuts of the pattern.
      float scratchGain = 1.0F;
      double velocity = 1.0;
      if (scratch_.active && !advanceScratch(velocity, scratchGain)) {
        if (scratch_.pattern == static_cast<int>(core::ScratchPattern::Backspin) ||
            scratch_.pattern == static_cast<int>(core::ScratchPattern::Brake)) {
          playing_.store(false, std::memory_order_relaxed);  // a backspin ends with the record stopped
          targetPlaying_.store(false, std::memory_order_relaxed);
          playRamp_ = 0.0F;
          break;
        }
        head = scratch_.anchor + scratch_.length * step;  // carry on where the record would be without the scratch
        addJump(i);
      }

      if (loopActive_.load(std::memory_order_relaxed)) {
        const double lStart = static_cast<double>(loopStartFrame_.load(std::memory_order_relaxed));
        const double lEnd = static_cast<double>(loopEndFrame_.load(std::memory_order_relaxed));
        if (lEnd > lStart && head >= lEnd) {
          head = lStart + std::fmod(head - lStart, lEnd - lStart);
          addJump(i);
        }
      }

      if (head >= static_cast<double>(totalFrames)) {
        playing_.store(false, std::memory_order_relaxed);
        targetPlaying_.store(false, std::memory_order_relaxed);
        playRamp_ = 0.0F;
        break;
      }

      const float amp = playRamp_ * currentGainLinear_ * currentVolume_ * scratchGain;
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

      head = std::max(0.0, head + step * velocity);
    }
  }

  applyDeclick(outputChannels, numChannels, numSamples, jumps.at.data(), jumps.count);

  // A seek or cue that arrived while this block rendered owns the position: do not overwrite it.
  if (positionEpoch_.load(std::memory_order_acquire) == epoch) {
    playhead_.store(head, std::memory_order_relaxed);
  }
}

void DeckPlayer::forgetStretch() noexcept {
  reprimePending_ = false;
  // Silent deck: whatever the stretcher held belongs to a position that is about to change. Start from scratch.
  stretchPath_ = false;
  stretchExiting_ = false;
  stretchBlend_ = 0.0F;
  keylock_.invalidate();
}

void DeckPlayer::setKeyShift(float semitones) noexcept {
  if (!std::isfinite(semitones)) {
    return;
  }
  keyShiftSemitones_.store(std::clamp(semitones, -kMaxKeyShiftSemitones, kMaxKeyShiftSemitones),
                           std::memory_order_relaxed);
}

bool DeckPlayer::wantsStretch(double speed, const TrackBuffer* reference) const noexcept {
  if (scratch_.active || reference == nullptr || !(speed >= TimeStretcher::kMinRatio && speed <= 3.5)) {
    return false;  // a record being moved by hand is varispeed by nature
  }
  const bool keylockOn = keylockEnabled_.load(std::memory_order_relaxed);
  const bool shifted = std::fabs(keyShiftSemitones_.load(std::memory_order_relaxed)) > 0.001F;
  const double tempoEpsilon = stretchPath_ ? 0.0004 : 0.001;  // hysteresis around exactly 1.0x
  const bool retuned = keylockOn && std::fabs(speed - 1.0) > tempoEpsilon;
  // A key shift that was just taken off glides back first: leaving the stretcher in the middle of the glide would be an
  // audible pitch jump. Plain varispeed has the pitch of the tempo (keylock off) or none (keylock on).
  const float varispeedSemitones = keylockOn ? 0.0F : static_cast<float>(12.0 * std::log2(speed));
  const bool settling = stretchPath_ && std::fabs(keylock_.currentSemitones() - varispeedSemitones) > 0.004F;
  if (!shifted && !retuned && !settling) {
    return false;  // nothing to correct: the untouched signal is the best signal
  }
  if (loopActive_.load(std::memory_order_relaxed)) {
    const double length = static_cast<double>(loopEndFrame_.load(std::memory_order_relaxed) -
                                              loopStartFrame_.load(std::memory_order_relaxed));
    if (length > 0.0 && length < kShortLoopSeconds * reference->sampleRate()) {
      return false;  // a loop roll has to bite at once; the stretcher's latency would smear it
    }
  }
  return true;
}

// RT
void DeckPlayer::readSource(void* context, double position, double step, int frames, float* left,
                            float* right) noexcept {
  auto* self = static_cast<DeckPlayer*>(context);
  if (!self->sourceStems_) {
    readResampled(self->sourceMaster_, 0, position, step, frames, left);
    readResampled(self->sourceMaster_, 1, position, step, frames, right);
    return;
  }
  // The stems are mixed first (their volumes, mutes, EQ and filters), then the one stretcher handles the sum.
  for (int done = 0; done < frames; done += kStemReadChunk) {
    const int n = std::min(kStemReadChunk, frames - done);
    const double at = position + static_cast<double>(done) * step;
    for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
      readResampled(self->sourceStemBufs_[s], 0, at, step, n, self->stemScratchL_[s].data());
      readResampled(self->sourceStemBufs_[s], 1, at, step, n, self->stemScratchR_[s].data());
    }
    const float* inL[core::kStemKindCount] = {self->stemScratchL_[0].data(), self->stemScratchL_[1].data(),
                                              self->stemScratchL_[2].data(), self->stemScratchL_[3].data()};
    const float* inR[core::kStemKindCount] = {self->stemScratchR_[0].data(), self->stemScratchR_[1].data(),
                                              self->stemScratchR_[2].data(), self->stemScratchR_[3].data()};
    self->stemMixer_.process(inL, inR, left + done, right + done, n);
  }
}

// RT
void DeckPlayer::renderStretched(float* const* out, int numChannels, int numSamples, const BlockParams& block,
                                 JumpList& jumps, double& head) noexcept {
  // Tempo changes keep the pitch (the stretcher's job); with keylock off the pitch follows the tempo like a record.
  const float tempoSemitones =
      keylockEnabled_.load(std::memory_order_relaxed) ? 0.0F : static_cast<float>(12.0 * std::log2(block.speed));
  keylock_.setTargetSemitones(keyShiftSemitones_.load(std::memory_order_relaxed) + tempoSemitones);

  const bool loopOn = loopActive_.load(std::memory_order_relaxed);
  const double loopStart = static_cast<double>(loopStartFrame_.load(std::memory_order_relaxed));
  const double loopEnd = static_cast<double>(loopEndFrame_.load(std::memory_order_relaxed));

  for (int i = 0; i < numSamples; ++i) {
    playRamp_ += rampCoeff_ * ((block.targetPlay ? 1.0F : 0.0F) - playRamp_);
    if (playRamp_ < 1e-4F && !block.targetPlay) {
      playing_.store(false, std::memory_order_relaxed);
      playRamp_ = 0.0F;
      keylock_.invalidate();
      break;
    }
    currentGainLinear_ += rampCoeff_ * (block.targetGainLinear - currentGainLinear_);
    currentVolume_ += rampCoeff_ * (block.targetVolume - currentVolume_);

    if (loopOn && loopEnd > loopStart && head >= loopEnd) {
      head = loopStart + std::fmod(head - loopStart, loopEnd - loopStart);
      jumps.add(i);
      // A wrap is a seek: the stretcher restarts at the loop start, sample accurately, once this deck's turn at the prime
      // budget comes. Until then it keeps running from its current state (right pitch, a few ms of stale content, hidden
      // by the declick) instead of dropping to the untouched signal.
      if (keylock_.primed()) {
        reprimePending_ = true;
      }
    }
    if (reprimePending_ && primeAllowed_) {
      reprimePending_ = false;
      if (keylock_.primed()) {
        keylock_.invalidate();
        jumps.add(i);  // the stretcher content jumps to the loop start here
      }
    }
    if (head >= static_cast<double>(block.totalFrames)) {
      playing_.store(false, std::memory_order_relaxed);
      targetPlaying_.store(false, std::memory_order_relaxed);
      playRamp_ = 0.0F;
      keylock_.invalidate();
      break;
    }
    // Weight of the untouched signal: fades in when the stretcher is being left, out when it has just been entered.
    if (block.blendable) {
      const float goal = stretchExiting_ ? 1.0F : 0.0F;
      stretchBlend_ += std::clamp(goal - stretchBlend_, -stretchBlendStep_, stretchBlendStep_);
    }
    bool stretcherNeeded = !(block.blendable && stretchBlend_ >= 1.0F);
    bool waitingForPrime = false;
    if (stretcherNeeded && !keylock_.primed() && !primeAllowed_) {
      // Priming costs about half a millisecond; the graph lets one deck per block do it (in turn). This one plays the
      // untouched signal meanwhile (full mix) or a moment of silence (stems, scratch) and starts once it is its turn.
      if (block.blendable) {
        stretchBlend_ = 1.0F;
        stretcherNeeded = false;
      } else {
        stretcherNeeded = false;
        waitingForPrime = true;
      }
    }
    if (!stretcherNeeded && !waitingForPrime) {
      keylock_.invalidate();  // it is not being fed while the untouched signal plays alone: restart if it is needed again
    }
    float left = 0.0F;
    float right = 0.0F;
    if (stretcherNeeded) {
      if (!keylock_.primed()) {
        keylock_.prime(&DeckPlayer::readSource, this, head, block.srcStep, block.speed);
        stretchEpoch_ = block.epoch;
        primedThisBlock_ = true;
        reprimePending_ = false;
        if (!block.blendable) {
          jumps.add(i);  // blended starts are continuous (the stretcher begins aligned with what plays); the rest jumps
        }
      }
      keylock_.nextFrame(&DeckPlayer::readSource, this, block.srcStep, block.speed, left, right);
    }
    if (block.blendable && stretchBlend_ > 0.0F) {
      const float w = stretchBlend_;
      const std::int64_t length = sourceMaster_->numFrames();
      const float directL = cubicAt(sourceMaster_->channelData(0), length, head);
      const float directR = cubicAt(sourceMaster_->channelData(std::min(1, sourceMaster_->numChannels() - 1)), length, head);
      left = left * (1.0F - w) + directL * w;
      right = right * (1.0F - w) + directR * w;
    }

    const float amp = playRamp_ * currentGainLinear_ * currentVolume_;
    for (int ch = 0; ch < numChannels; ++ch) {
      if (out[ch] != nullptr) {
        out[ch][i] = (ch == 0 ? left : right) * amp;
      }
    }
    head = std::max(0.0, head + block.step);
  }
}

void DeckPlayer::startScratch(int pattern, double beats, double beatSeconds) noexcept {
  if (!playing_.load(std::memory_order_relaxed) || beats <= 0.0 || beatSeconds <= 0.0 ||
      pattern > static_cast<int>(core::ScratchPattern::Backspin)) {
    return;  // the record has to be turning
  }
  scratch_.active = true;
  scratch_.pattern = pattern;
  scratch_.pos = 0.0;
  scratch_.beatSamples = beatSeconds * deviceSampleRate_;
  scratch_.length = beats * scratch_.beatSamples;
  scratch_.anchor = playhead_.load(std::memory_order_relaxed);
  scratch_.epoch = positionEpoch_.load(std::memory_order_acquire);
  scratch_.gate = 1.0;
}

bool DeckPlayer::advanceScratch(double& velocity, float& gain) noexcept {
  if (scratch_.pos >= scratch_.length) {
    scratch_.active = false;
    return false;
  }
  constexpr double kTwoPi = 6.283185307179586;
  const double beat = scratch_.pos / scratch_.beatSamples;  // beats since the scratch began
  const double progress = scratch_.pos / scratch_.length;
  const double stroke = std::sin(kTwoPi * beat * 2.0);      // two forward-back strokes per beat
  double gate = 1.0;
  switch (static_cast<core::ScratchPattern>(scratch_.pattern)) {
    case core::ScratchPattern::Baby:
      velocity = 2.4 * stroke;
      break;
    case core::ScratchPattern::Transformer:
      velocity = 2.0 * stroke;
      gate = (static_cast<int>(beat * 8.0) % 2 == 0) ? 1.0 : 0.0;  // the fader chops on 1/8 notes
      break;
    case core::ScratchPattern::Chirp:
      velocity = 2.6 * stroke;
      gate = std::fmod(beat * 2.0, 1.0) < 0.4 ? 1.0 : 0.0;  // open on the push, closed on the pull
      break;
    case core::ScratchPattern::Flare: {
      velocity = 2.4 * stroke;
      const double half = std::fmod(beat * 4.0, 1.0);  // position inside each half stroke
      gate = (half > 0.45 && half < 0.6) ? 0.0 : 1.0;
      break;
    }
    case core::ScratchPattern::Crab:
      velocity = 2.0 * stroke;
      gate = std::fmod(beat * 16.0, 1.0) < 0.55 ? 1.0 : 0.0;
      break;
    case core::ScratchPattern::Scribble:
      velocity = 1.0 + 0.9 * std::sin(kTwoPi * beat * 12.0);
      break;
    case core::ScratchPattern::Tear: {
      const double p = std::fmod(beat * 2.0, 1.0);
      velocity = p < 0.22 ? 3.0 : (p < 0.3 ? 0.0 : (p < 0.5 ? 3.0 : -2.6));
      break;
    }
    case core::ScratchPattern::Stab: {
      const double p = std::fmod(beat * 2.0, 1.0);
      velocity = p < 0.15 ? 3.0 : (p < 0.3 ? -3.0 : 0.0);
      gate = p < 0.15 ? 1.0 : 0.0;  // only the forward stab is heard
      break;
    }
    case core::ScratchPattern::Drag:
      velocity = 0.2 + 0.6 * std::sin(kTwoPi * beat);
      break;
    case core::ScratchPattern::Brake:
      velocity = std::pow(1.0 - progress, 1.6);  // the platter slowing down; the pitch falls with it
      gate = progress < 0.85 ? 1.0 : (1.0 - progress) / 0.15;
      break;
    case core::ScratchPattern::Backspin:
      velocity = -6.0 * (1.0 - progress) * (1.0 - progress);
      gate = 1.0 - progress;
      break;
  }
  scratch_.gate += (gate - scratch_.gate) * scratchGateCoeff_;
  gain = static_cast<float>(scratch_.gate);
  scratch_.pos += 1.0;
  return true;
}

void DeckPlayer::releaseTail(float* const* out, int numChannels, int numSamples) noexcept {
  constexpr float kSilent = 1.0e-5F;
  if (std::fabs(lastOutL_) <= kSilent && std::fabs(lastOutR_) <= kSilent) {
    lastOutL_ = lastOutR_ = 0.0F;
    declickGain_ = 0.0F;
    return;
  }
  constexpr int kAtStart = 0;  // the block is silent: this is the last value fading out
  applyDeclick(out, numChannels, numSamples, &kAtStart, 1);
}

void DeckPlayer::applyDeclick(float* const* out, int numChannels, int numSamples, const int* jumpsAt,
                              int jumpCount) noexcept {
  float* left = (numChannels > 0) ? out[0] : nullptr;
  float* right = (numChannels > 1 && out[1] != nullptr) ? out[1] : left;
  if (left == nullptr || numSamples <= 0) {
    return;
  }
  int next = 0;
  for (int i = 0; i < numSamples; ++i) {
    if (next < jumpCount && jumpsAt[next] == i) {
      // The correction that makes the new signal start where the old one ended, then decays to nothing.
      declickL_ = (i > 0 ? left[i - 1] : lastOutL_) - left[i];
      declickR_ = (i > 0 ? right[i - 1] : lastOutR_) - right[i];
      declickGain_ = 1.0F;
      ++next;
    }
    if (declickGain_ > 1.0e-4F) {
      left[i] += declickL_ * declickGain_;
      if (right != left) {
        right[i] += declickR_ * declickGain_;
      }
      declickGain_ *= declickDecay_;
    }
  }
  lastOutL_ = left[numSamples - 1];
  lastOutR_ = right[numSamples - 1];
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
  std::lock_guard<std::mutex> lock(controlMutex_);
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

void DeckPlayer::setLoopSeconds(double startSeconds, double endSeconds, bool active) noexcept {
  const auto* buffer = activeBuffer_.load(std::memory_order_relaxed);
  if (buffer == nullptr && hasStems_.load(std::memory_order_relaxed)) {
    buffer = activeStemBuffers_[0].load(std::memory_order_relaxed);
  }
  if (buffer == nullptr || buffer->sampleRate() <= 0.0) {
    return;
  }
  const double rate = buffer->sampleRate();
  setLoop(static_cast<std::int64_t>(std::max(0.0, startSeconds) * rate),
          static_cast<std::int64_t>(std::max(0.0, endSeconds) * rate));
  setLoopActive(active);
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
