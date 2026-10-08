// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "Core/State/Ids.hpp"

namespace zyron::core {

struct CuePointTelemetry {
  int index{0};             // 1..8
  std::int64_t frame{0};
  double timeSec{0.0};
  std::string name;
  std::string color;
  std::string type{"cue"};  // "cue", "loop", "intro", "drop", "break", "outro", "memory"
};

struct LoopTelemetry {
  bool active{false};
  std::int64_t startFrame{0};
  std::int64_t endFrame{0};
  double startTimeSec{0.0};
  double endTimeSec{0.0};
};

struct BeatgridTelemetry {
  double bpm{0.0};
  std::int64_t firstBeatFrame{0};
  double firstBeatTimeSec{0.0};
  double beatIntervalSec{0.0};
  double beatIntervalFrames{0.0};
};

/// Realtime telemetry snapshot published from the audio engine to the UI at <= 60 Hz (ARCHITECTURE section 6).
struct DeckTelemetry {
  DeckId deck{DeckId::A};
  bool hasTrack{false};
  bool isPlaying{false};
  std::int64_t currentFrame{0};
  double currentTimeSec{0.0};
  double durationSec{0.0};
  double playbackSpeed{1.0};
  LoopTelemetry loop;
  std::array<std::optional<CuePointTelemetry>, 8> hotCues{};
  BeatgridTelemetry beatgrid{};
  float vuLevelLeft{0.0f};
  float vuLevelRight{0.0f};
  bool hasStems{false};
  std::array<float, kStemKindCount> stemVuLevels{};
};

}  // namespace zyron::core
