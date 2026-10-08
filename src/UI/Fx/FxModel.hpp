// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

#include "Core/State/Ids.hpp"

namespace zyron::ui {

/// Names shown for the FX types, their main knob and the hits (translated).
[[nodiscard]] juce::String fxTypeLabel(core::FxType type);
[[nodiscard]] juce::String fxParamLabel(core::FxType type);
[[nodiscard]] juce::String fxHitLabel(core::FxHitType type);

/// Beat lengths the engine accepts (Command limits): 400 BPM .. 20 BPM.
inline constexpr double kFxBeatSecondsMin = 0.15;
inline constexpr double kFxBeatSecondsMax = 3.0;

/// Decides when a deck's FX tempo (SetFxTempo) has to be sent again: when the BPM or the playback speed moved the beat
/// length by more than 0.5 %, and at most every `kMinTicksBetween` timer ticks (a few times a second while a pitch fader
/// is dragged).
class FxTempoTracker {
 public:
  static constexpr double kMinRelativeChange = 0.005;
  static constexpr int kMinTicksBetween = 10;

  /// The beat length to send now, or nothing. `tick` is a steadily growing timer tick count.
  [[nodiscard]] std::optional<double> update(double bpm, double playbackSpeed, long tick) {
    if (!(bpm > 0.0) || !(playbackSpeed > 0.0)) {
      return std::nullopt;
    }
    const double beat = std::clamp(60.0 / (bpm * playbackSpeed), kFxBeatSecondsMin, kFxBeatSecondsMax);
    if (lastSent_ > 0.0) {
      if (std::abs(beat - lastSent_) / lastSent_ <= kMinRelativeChange || tick - lastTick_ < kMinTicksBetween) {
        return std::nullopt;
      }
    }
    lastSent_ = beat;
    lastTick_ = tick;
    return beat;
  }

 private:
  double lastSent_{0.0};
  long lastTick_{0};
};

/// What a deck contributes to the tempo of an FX hit.
struct DeckBeat {
  bool playing{false};
  double bpm{0.0};
  double playbackSpeed{1.0};
};

/// Beat length (60 / BPM of the playing speed) of the first playing deck that has a tempo; 0 = free (no deck to follow).
[[nodiscard]] inline double fxHitBeatSeconds(std::span<const DeckBeat> decks) {
  for (const DeckBeat& deck : decks) {
    if (deck.playing && deck.bpm > 0.0 && deck.playbackSpeed > 0.0) {
      return std::clamp(60.0 / (deck.bpm * deck.playbackSpeed), kFxBeatSecondsMin, kFxBeatSecondsMax);
    }
  }
  return 0.0;
}

}  // namespace zyron::ui
