// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace zyron::ui {

/// The pitch change the listener hears, in semitones: the key shift, plus the varispeed pitch when keylock is off
/// (a tempo of 1.06 without keylock is about one semitone up).
[[nodiscard]] inline double effectiveSemitones(float keyShift, bool keylock, double playbackSpeed) {
  const double varispeed = (!keylock && playbackSpeed > 0.0) ? 12.0 * std::log2(playbackSpeed) : 0.0;
  return static_cast<double>(keyShift) + varispeed;
}

/// A Camelot key ("8A", "11B") moved by `semitones` (rounded to a whole semitone): one semitone is +7 on the wheel.
/// Anything that is not Camelot notation (empty, "Am", garbage) comes back unchanged.
[[nodiscard]] inline std::string shiftCamelot(const std::string& key, double semitones) {
  if (key.size() < 2 || key.size() > 3) {
    return key;
  }
  const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(key.back())));
  const std::string digits = key.substr(0, key.size() - 1);
  if ((letter != 'A' && letter != 'B') || digits.empty() ||
      digits.find_first_not_of("0123456789") != std::string::npos) {
    return key;
  }
  const int number = std::stoi(digits);
  if (number < 1 || number > 12) {
    return key;
  }
  const long steps = std::lround(semitones);
  const long moved = (((number - 1 + 7 * steps) % 12) + 12) % 12;
  return std::to_string(moved + 1) + letter;
}

/// "+2.0 st" / "-1.5 st" / "0.0 st" for the key-shift label.
[[nodiscard]] inline std::string formatSemitones(double semitones) {
  char buffer[24];
  if (std::fabs(semitones) < 0.05) {
    return "0.0 st";
  }
  std::snprintf(buffer, sizeof(buffer), "%+.1f st", semitones);
  return buffer;
}

}  // namespace zyron::ui
