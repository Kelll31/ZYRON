// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Effects/FxUnit.hpp"

#include <algorithm>
#include <cmath>

#include "Audio/Effects/DelayEffect.hpp"
#include "Audio/Effects/EchoEffect.hpp"
#include "Audio/Effects/FlangerEffect.hpp"
#include "Audio/Effects/PhaserEffect.hpp"
#include "Audio/Effects/ReverbEffect.hpp"

namespace zyron::audio {

namespace {

constexpr double kFadeSeconds = 0.020;   // wet amount smoothing, also the fade-out of a replaced effect
constexpr double kRetireSeconds = 0.30;  // an insert effect is cleared this long after it was switched off
constexpr float kSilentSend = 1.0e-4F;
constexpr float kFlipSend = 1.0e-3F;  // -60 dB: the tail has ducked far enough to move it behind / before the fader
constexpr float kDefaultBeatSeconds = 0.375F;
constexpr float kMaxEchoMs = 1500.0F;
constexpr float kMaxDelayMs = 2000.0F;

bool isSendEffect(core::FxType type) noexcept {
  return type == core::FxType::Echo || type == core::FxType::Delay || type == core::FxType::Reverb;
}

float lerp(float a, float b, float t) noexcept {
  return a + (b - a) * t;
}

}  // namespace

FxUnit::FxUnit() = default;
FxUnit::~FxUnit() = default;

void FxUnit::prepare(double sampleRate, int maxBlockSize) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  maxBlock_ = std::max(maxBlockSize, 16);
  smoothCoeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate_ * kFadeSeconds)));

  bank_[core::index(core::FxType::Echo)] = std::make_unique<EchoEffect>();
  bank_[core::index(core::FxType::Reverb)] = std::make_unique<ReverbEffect>();
  bank_[core::index(core::FxType::Flanger)] = std::make_unique<FlangerEffect>();
  bank_[core::index(core::FxType::Phaser)] = std::make_unique<PhaserEffect>();
  bank_[core::index(core::FxType::Delay)] = std::make_unique<DelayEffect>();
  for (auto& effect : bank_) {
    if (effect != nullptr) {
      effect->prepare(sampleRate_, maxBlock_);
      effect->setEnabled(true);
      effect->reset();
    }
  }
  dryL_.assign(static_cast<std::size_t>(maxBlock_), 0.0F);
  dryR_.assign(static_cast<std::size_t>(maxBlock_), 0.0F);
  live_ = Voice{};
  outgoing_ = Voice{};
  if (enabled_ && type_ != core::FxType::None) {
    startVoice(type_);
    applyParameters(live_);
  }
}

void FxUnit::reset() noexcept {
  for (auto& effect : bank_) {
    if (effect != nullptr) {
      effect->reset();
    }
  }
  outgoing_ = Voice{};
  const core::FxType type = live_.effect != nullptr ? live_.type : core::FxType::None;
  live_ = Voice{};
  if (type != core::FxType::None && enabled_) {
    startVoice(type);
    applyParameters(live_);
  }
}

bool FxUnit::isActive() const noexcept {
  return live_.effect != nullptr || outgoing_.effect != nullptr;
}

void FxUnit::retire(Voice& voice) noexcept {
  if (voice.effect != nullptr) {
    voice.effect->reset();  // a few tens of microseconds; the effect is clean again for its next use
  }
  voice = Voice{};
}

bool FxUnit::startVoice(core::FxType type) noexcept {
  Effect* effect = bank_[core::index(type)].get();
  if (effect == nullptr) {
    live_ = Voice{};
    return false;
  }
  if (outgoing_.effect == effect) {
    // Switched away and straight back: take the ringing effect over instead of cutting its tail.
    live_ = outgoing_;
    outgoing_ = Voice{};
    live_.wanted = true;
    return false;
  }
  live_ = Voice{};
  live_.effect = effect;
  live_.type = type;
  live_.sendStyle = isSendEffect(type);
  live_.wanted = true;
  return true;
}

void FxUnit::applyParameters(Voice& voice) noexcept {
  Effect* fx = voice.effect;
  if (fx == nullptr) {
    return;
  }
  const float beat = beatSeconds_ > 0.0F ? beatSeconds_ : kDefaultBeatSeconds;
  const float wetNow = voice.wanted ? wet_ : 0.0F;
  switch (voice.type) {
    case core::FxType::Echo: {
      float ms = beat * 1000.0F;
      while (ms > kMaxEchoMs) {
        ms *= 0.5F;  // a very slow tempo: echo on the half beat instead of running out of buffer
      }
      fx->setParameterById("time", ms);
      fx->setParameterById("feedback", lerp(0.20F, 0.85F, param_));
      fx->setDryWet(1.0F);
      break;
    }
    case core::FxType::Delay: {
      float ms = beat * 750.0F;  // dotted eighth against the beat
      while (ms > kMaxDelayMs) {
        ms *= 0.5F;
      }
      fx->setParameterById("time", ms);
      fx->setParameterById("feedback", lerp(0.20F, 0.85F, param_));
      fx->setParameterById("pingpong", 1.0F);
      fx->setDryWet(1.0F);
      break;
    }
    case core::FxType::Reverb:
      fx->setParameterById("room_size", lerp(0.40F, 0.98F, param_));
      fx->setDryWet(1.0F);
      break;
    case core::FxType::Flanger:
    case core::FxType::Phaser: {
      const float rateHz = 0.1F * std::pow(20.0F, param_);  // 0.1 .. 2 Hz
      fx->setParameterById("rate", rateHz);
      fx->setDryWet(wetNow);
      break;
    }
    case core::FxType::None:
      break;
  }
  voice.sendTarget = (flipPending_ && voice.sendStyle) ? 0.0F : wetNow;
}

void FxUnit::configure(core::FxType type, bool enabled, float wet, float param, bool postFader) noexcept {
  bool freshVoice = false;
  if (!core::isValid(type)) {
    type = core::FxType::None;  // a message that did not come through validate(): never index the bank with it
  }
  if (postFader == postFader_) {
    if (flipPending_) {
      flipPending_ = false;  // changed back before it happened: the tails come back up
      applyParameters(live_);
    }
  } else if (sendsAudible()) {
    // Moving a ringing send tail across the fader would rescale it in one step. Duck the sends (smoothly), move, bring
    // them back.
    flipPending_ = true;
    pendingPostFader_ = postFader;
  } else {
    postFader_ = postFader;
    flipPending_ = false;
  }
  wet_ = std::isfinite(wet) ? std::clamp(wet, 0.0F, 1.0F) : wet_;
  param_ = std::isfinite(param) ? std::clamp(param, 0.0F, 1.0F) : param_;
  enabled_ = enabled && type != core::FxType::None;

  if (type != type_) {
    type_ = type;
    if (live_.effect != nullptr) {
      if (outgoing_.effect != nullptr && outgoing_.effect != live_.effect) {
        retire(outgoing_);  // two replacements in a row: the older one is cut (rare, and it was already fading)
      }
      outgoing_ = live_;
      outgoing_.wanted = false;
      applyParameters(outgoing_);
      live_ = Voice{};
    }
    if (enabled_) {
      freshVoice = startVoice(type);
    }
  } else if (enabled_ && live_.effect == nullptr) {
    freshVoice = startVoice(type);
  }
  if (live_.effect != nullptr) {
    live_.wanted = enabled_;
    applyParameters(live_);
    if (freshVoice) {
      // The effect is clean; resetting it now makes its smoothed values start at the targets instead of gliding there
      // from the defaults (a delay time sweep, a wet blip). An insert effect starts dry and fades its wet signal in.
      if (!live_.sendStyle) {
        live_.effect->setDryWet(0.0F);
      }
      live_.effect->reset();
      if (!live_.sendStyle) {
        live_.effect->setDryWet(wet_);
      }
    }
  }
}

void FxUnit::setBeatSeconds(float beatSeconds) noexcept {
  beatSeconds_ = std::isfinite(beatSeconds) && beatSeconds > 0.0F ? beatSeconds : kDefaultBeatSeconds;
  applyParameters(live_);
}

// RT
void FxUnit::processVoice(Voice& voice, float* const* channels, int numChannels, int numSamples) noexcept {
  Effect* fx = voice.effect;
  if (fx == nullptr) {
    return;
  }
  float* left = channels[0];
  float* right = (numChannels > 1 && channels[1] != nullptr) ? channels[1] : nullptr;

  if (!voice.sendStyle) {
    fx->process(channels, numChannels, numSamples);
    if (!voice.wanted) {
      voice.idleSamples += numSamples;
      if (static_cast<double>(voice.idleSamples) > kRetireSeconds * sampleRate_) {
        retire(voice);
      }
    }
    return;
  }

  for (int done = 0; done < numSamples; done += maxBlock_) {
    const int n = std::min(maxBlock_, numSamples - done);
    float* wetPlanes[2] = {left + done, right != nullptr ? right + done : nullptr};
    std::copy_n(wetPlanes[0], n, dryL_.data());
    if (right != nullptr) {
      std::copy_n(wetPlanes[1], n, dryR_.data());
    }
    fx->process(wetPlanes, right != nullptr ? 2 : 1, n);  // the effect is at 100 % wet: this is the effect signal only
    for (int i = 0; i < n; ++i) {
      voice.send += smoothCoeff_ * (voice.sendTarget - voice.send);
      wetPlanes[0][i] = dryL_[static_cast<std::size_t>(i)] + voice.send * wetPlanes[0][i];
      if (right != nullptr) {
        wetPlanes[1][i] = dryR_[static_cast<std::size_t>(i)] + voice.send * wetPlanes[1][i];
      }
    }
  }
  if (!voice.wanted && voice.send < kSilentSend) {
    retire(voice);  // faded out: clear the tail and stop spending CPU
  }
}

bool FxUnit::sendsAudible() const noexcept {
  return (live_.effect != nullptr && live_.sendStyle && live_.send > kFlipSend) ||
         (outgoing_.effect != nullptr && outgoing_.sendStyle && outgoing_.send > kFlipSend);
}

void FxUnit::finishFlipIfSilent() noexcept {
  if (!flipPending_ || sendsAudible()) {
    return;
  }
  postFader_ = pendingPostFader_;
  flipPending_ = false;
  applyParameters(live_);  // the send glides back up
}

// RT
void FxUnit::process(float* const* channels, int numChannels, int numSamples) noexcept {
  if (channels == nullptr || channels[0] == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }
  if (outgoing_.effect == nullptr && live_.effect == nullptr) {
    finishFlipIfSilent();
    return;
  }
  const int usable = std::min(numChannels, 2);
  processVoice(outgoing_, channels, usable, numSamples);
  processVoice(live_, channels, usable, numSamples);
  finishFlipIfSilent();
}

}  // namespace zyron::audio
