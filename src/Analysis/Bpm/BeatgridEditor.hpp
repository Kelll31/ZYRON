// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Analysis/Bpm/Beatgrid.hpp"

namespace zyron::analysis {

struct BarBeatPosition {
  std::int64_t barIndex{0};  // 0-indexed musical bar number
  int beatInBar{1};          // 1..4 in standard 4/4 meter
  double fraction{0.0};      // 0.0 .. 1.0 phase within the current beat
};

/// High-precision beatgrid editor with real-time editing, phase shifting, tap tempo,
/// quantization, and user-source protection (SPEC section 16, ROADMAP P3-05).
class BeatgridEditor {
 public:
  BeatgridEditor() = default;
  explicit BeatgridEditor(BeatgridData data);

  [[nodiscard]] const BeatgridData& grid() const noexcept { return data_; }
  [[nodiscard]] double bpm() const noexcept { return data_.bpm; }
  [[nodiscard]] std::int64_t firstBeatFrame() const noexcept { return data_.firstBeatFrame; }
  [[nodiscard]] int sampleRate() const noexcept { return data_.sampleRate; }
  [[nodiscard]] int downbeatOffset() const noexcept { return data_.downbeatOffset; }
  [[nodiscard]] bool isUserModified() const noexcept { return data_.source == "user"; }
  [[nodiscard]] const std::string& source() const noexcept { return data_.source; }

  // --- Editing operations (§16) ---

  /// Sets the BPM directly, maintaining firstBeatFrame as anchor. Marks source as "user".
  void setBpm(double newBpm);

  /// Nudges BPM by delta (e.g. +0.1, -0.1). Marks source as "user".
  void adjustBpm(double deltaBpm);

  /// Sets the first beat anchor frame. Marks source as "user".
  void setFirstBeat(std::int64_t frame);

  /// Shifts the entire grid by deltaFrames sample frames. Marks source as "user".
  void shiftPhase(std::int64_t deltaFrames);

  /// Shifts the entire grid by deltaMs milliseconds. Marks source as "user".
  void shiftPhaseMs(double deltaMs);

  /// Sets which beat in the cycle is the bar start (0..3). Marks source as "user".
  void setDownbeatOffset(int offset);

  /// Tap tempo calculation: records a tap at tapFrame; updates BPM once >= 3 intervals exist.
  void tapTempo(std::int64_t tapFrame);

  /// Resets tap tempo history.
  void resetTapTempo() noexcept;

  /// Recalculates grid for changed audio sample rate.
  void setSampleRate(int newSampleRate);

  /// Sets source explicitly ("auto" or "user").
  void setSource(std::string_view source);

  // --- Grid Query & Quantization ---

  /// Finds the sample frame of the beat nearest to currentFrame.
  [[nodiscard]] std::int64_t findNearestBeat(std::int64_t currentFrame) const;

  /// Finds the sample frame of the next upcoming beat.
  [[nodiscard]] std::int64_t findNextBeat(std::int64_t currentFrame) const;

  /// Finds the sample frame of the previous beat.
  [[nodiscard]] std::int64_t findPreviousBeat(std::int64_t currentFrame) const;

  /// Returns phase fraction in range [0.0, 1.0) within current beat.
  [[nodiscard]] double getBeatFraction(std::int64_t currentFrame) const;

  /// Returns bar and beat in bar for the specified audio frame.
  [[nodiscard]] BarBeatPosition getBarBeat(std::int64_t currentFrame) const;

  /// Generates all beat frames within the specified total duration.
  [[nodiscard]] std::vector<std::int64_t> generateBeatFrames(std::size_t totalAudioFrames) const;

  /// Generates all downbeat frames within the specified total duration.
  [[nodiscard]] std::vector<std::int64_t> generateDownbeatFrames(std::size_t totalAudioFrames) const;

  // --- Serialization ---
  [[nodiscard]] std::string toJson() const { return data_.toJson(); }
  bool fromJson(std::string_view json) { return BeatgridData::fromJson(json, data_); }

 private:
  BeatgridData data_;
  std::vector<std::int64_t> tapHistory_;
};

}  // namespace zyron::analysis
