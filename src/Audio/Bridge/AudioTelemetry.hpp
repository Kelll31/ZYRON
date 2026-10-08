// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstdint>

#include "Core/State/Ids.hpp"

namespace zyron::audio {

/// Telemetry facts published by the realtime audio thread for one deck (SPEC section 14, ARCHITECTURE section 6).
struct DeckTelemetry {
  std::int64_t playheadSample{0};
  double playheadSeconds{0.0};
  double trackDurationSeconds{0.0};
  bool isPlaying{false};
  bool isCueing{false};
  float peakLeft{0.0F};
  float peakRight{0.0F};
  bool hasStems{false};
  double playbackSpeed{1.0};
  std::array<float, core::kStemKindCount> stemPeaks{};

  friend bool operator==(const DeckTelemetry&, const DeckTelemetry&) = default;
};

/// Complete audio engine telemetry snapshot published to UI at <= 60 Hz (ARCHITECTURE section 6).
struct AudioTelemetry {
  std::array<DeckTelemetry, core::kDeckCount> decks{};
  float masterPeakLeft{0.0F};
  float masterPeakRight{0.0F};
  float limiterGainReduction{1.0F};
  std::uint64_t droppedRtMessages{0};
  std::uint64_t callbackCount{0};
  double dspLoadPercent{0.0};
  std::uint64_t timestampNs{0};

  friend bool operator==(const AudioTelemetry&, const AudioTelemetry&) = default;
};

}  // namespace zyron::audio
