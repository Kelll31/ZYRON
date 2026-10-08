// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "Audio/Effects/Effect.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// One FX slot of a channel strip driven by the SetFx / SetFxTempo commands (SPEC section 23).
///
/// Every effect type is built once in prepare(), so choosing another type on the audio thread allocates nothing: the old
/// effect keeps ringing out under a fade while the new one takes over. The mixer and the strip know nothing about the
/// effects themselves, only this interface.
///
/// Signal model:
///  - Echo, delay and reverb are *sends*: out = dry + wet * effect(dry). The dry signal is untouched, so engaging an echo
///    never dips the level, and wet = 0 is bit-exact bypass.
///  - Flanger and phaser are insert effects: out = (1 - wet) * dry + wet * effect(dry).
///  - Disabling fades the effect out over ~20 ms and then idles it (state cleared, no CPU).
///
/// What `param` (0..1) does: echo/delay feedback 0.2..0.85; reverb room size 0.4..0.98; flanger/phaser rate 0.1..2 Hz
/// (exponential). Echo time = 1 beat, delay time = 3/4 beat (ping-pong), both from setBeatSeconds() (375 ms until set).
///
/// All methods except prepare() are audio-thread only and allocation-free.
class FxUnit {
 public:
  FxUnit();
  ~FxUnit();

  FxUnit(const FxUnit&) = delete;
  FxUnit& operator=(const FxUnit&) = delete;

  /// Non-realtime: builds the effect bank and the scratch buffer.
  void prepare(double sampleRate, int maxBlockSize);
  void reset() noexcept;

  /// Selects the effect and its settings. Changing `type` fades the previous effect out. `wet` / `param` are 0..1.
  void configure(core::FxType type, bool enabled, float wet, float param, bool postFader) noexcept;
  /// Length of a beat in seconds for the tempo-synced effects (<= 0: default 375 ms).
  void setBeatSeconds(float beatSeconds) noexcept;

  [[nodiscard]] core::FxType type() const noexcept { return type_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool postFader() const noexcept { return postFader_; }
  /// True while the unit still produces sound or consumes CPU (enabled, fading or ringing out).
  [[nodiscard]] bool isActive() const noexcept;

  /// In place, up to two channels; any block length.
  void process(float* const* channels, int numChannels, int numSamples) noexcept;

 private:
  struct Voice {
    Effect* effect{nullptr};
    core::FxType type{core::FxType::None};
    bool sendStyle{false};
    float send{0.0F};        // smoothed wet amount of a send effect
    float sendTarget{0.0F};
    int idleSamples{0};      // samples spent fully faded out; the effect is cleared once this is long enough
    bool wanted{false};      // false: fading out
  };

  void applyParameters(Voice& voice) noexcept;
  /// Makes `type` the live voice; true when it is a fresh one (false: a ringing-out effect was taken over).
  bool startVoice(core::FxType type) noexcept;
  void processVoice(Voice& voice, float* const* channels, int numChannels, int numSamples) noexcept;
  void retire(Voice& voice) noexcept;
  [[nodiscard]] bool sendsAudible() const noexcept;
  void finishFlipIfSilent() noexcept;

  double sampleRate_{48000.0};
  float smoothCoeff_{0.002F};
  int maxBlock_{1024};
  std::array<std::unique_ptr<Effect>, core::kFxTypeCount> bank_{};  // indexed by FxType; None stays empty

  core::FxType type_{core::FxType::None};
  bool enabled_{false};
  bool postFader_{true};
  bool flipPending_{false};      // a before/after-fader change waits for the send tails to duck out of the way
  bool pendingPostFader_{true};
  float wet_{0.5F};
  float param_{0.5F};
  float beatSeconds_{0.375F};

  Voice live_{};
  Voice outgoing_{};
  std::vector<float> dryL_;
  std::vector<float> dryR_;
};

}  // namespace zyron::audio
