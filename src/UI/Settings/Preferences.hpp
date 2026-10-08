// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <optional>
#include <string>

namespace zyron::ui {

/// General settings that are remembered between runs (preferences.txt next to language.txt).
struct Preferences {
  bool autoLoudness{true};  // trim the loudness of every track that arrives on a deck towards the target
  bool glue{true};          // master glue compressor
  bool limiter{true};       // master brickwall limiter
  bool autoStems{true};     // separate every loaded track into stems in the background, ready before it is needed

  friend bool operator==(const Preferences&, const Preferences&) = default;
};

inline constexpr double kLoudnessTargetLufs = -9.0;   // the level club tracks are balanced to
inline constexpr double kLoudnessTrimLimitDb = 12.0;  // the engine accepts -12..+12 dB

/// The trim that brings a track of `lufs` to the target, clamped to the engine's range. Empty when the track has not
/// been measured (0.0 = unknown, real values are negative).
[[nodiscard]] std::optional<float> loudnessTrimDb(double lufs);

[[nodiscard]] std::string formatPreferences(const Preferences& preferences);
/// Defensive: unknown keys and junk lines are ignored, a missing key keeps its default (on).
[[nodiscard]] Preferences parsePreferences(const std::string& text);

[[nodiscard]] Preferences loadPreferences();
void savePreferences(const Preferences& preferences);

}  // namespace zyron::ui
