// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Audio/Deck/DeckPlayer.hpp"
#include "Audio/Deck/SyncManager.hpp"

namespace zyron::audio {

constexpr std::size_t kMaxHotCues = 8;

enum class CueType {
  Cue,     // Standard hot cue point
  Loop,    // Hot loop
  Intro,   // Intro marker
  Drop,    // Main drop marker
  Break,   // Breakdown marker
  Outro,   // Outro marker
  Memory   // Memory cue
};

struct HotCuePoint {
  int index{0};                  // 0..7
  std::int64_t frame{0};         // Sample position
  std::string name;              // User or detected name
  std::string color{"#00FF88"};  // Hex color string
  CueType type{CueType::Cue};
  bool active{false};            // Whether slot is set
};

/// High-level Hot Cue and Loop controller for a deck (SPEC sections 25, 26, ROADMAP P3-09).
/// Real-time safe for audio thread playback operations.
class CueLoopManager {
 public:
  CueLoopManager() = default;
  ~CueLoopManager() = default;

  // --- Hot Cues ×8 (§25) ---

  /// Sets a hot cue at the given index (0..7).
  bool setHotCue(int index, std::int64_t frame, std::string_view name = "",
                 std::string_view color = "", CueType type = CueType::Cue);

  /// Clears a hot cue at the given index.
  bool clearHotCue(int index);

  /// Returns the hot cue at the given index if active.
  [[nodiscard]] std::optional<HotCuePoint> getHotCue(int index) const;

  /// Returns true if a hot cue is set at index.
  [[nodiscard]] bool hasHotCue(int index) const noexcept;

  /// Jumps the player to the hot cue point and optionally resumes playing.
  bool jumpToHotCue(DeckPlayer& player, int index, bool startPlaying = true);

  /// Access all 8 hot cue slots.
  [[nodiscard]] const std::array<HotCuePoint, kMaxHotCues>& allHotCues() const noexcept {
    return hotCues_;
  }

  // --- Loops (§26) ---

  /// Manual Loop In: sets the start point of a loop at frame.
  void setLoopIn(DeckPlayer& player, std::int64_t frame);

  /// Manual Loop Out: sets the end point and activates loop playback.
  void setLoopOut(DeckPlayer& player, std::int64_t frame);

  /// Auto Beat Loop: activates a quantized beat loop of specified length (1/32 .. 32 beats)
  /// starting from the nearest beat or current frame.
  void setBeatLoop(DeckPlayer& player, double beats, const DeckGrid& grid);

  /// Halves the current loop length (minimum 1/32 beat).
  void halveLoop(DeckPlayer& player, const DeckGrid& grid);

  /// Doubles the current loop length (maximum 32 beats).
  void doubleLoop(DeckPlayer& player, const DeckGrid& grid);

  /// Reloop: enables looping and jumps playhead to loop start if currently outside.
  void reloop(DeckPlayer& player);

  /// Exit: disables looping, allowing playhead to continue beyond loop end.
  void exitLoop(DeckPlayer& player);

  /// Moves the loop window forward or backward by beat count.
  void moveLoop(DeckPlayer& player, double beatsDelta, const DeckGrid& grid);

  // Status accessors
  [[nodiscard]] bool isLoopActive() const noexcept { return loopActive_; }
  [[nodiscard]] std::int64_t loopStartFrame() const noexcept { return loopStartFrame_; }
  [[nodiscard]] std::int64_t loopEndFrame() const noexcept { return loopEndFrame_; }
  [[nodiscard]] double currentLoopBeats() const noexcept { return currentLoopBeats_; }

 private:
  std::array<HotCuePoint, kMaxHotCues> hotCues_{};
  bool loopActive_{false};
  std::int64_t loopStartFrame_{0};
  std::int64_t loopEndFrame_{0};
  double currentLoopBeats_{4.0};
  bool loopInSet_{false};
};

}  // namespace zyron::audio
