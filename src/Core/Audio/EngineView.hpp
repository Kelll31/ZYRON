// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "Core/Audio/WaveformData.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// What the audio thread reports about one deck: facts, not requests (ARCHITECTURE section 6). The UI renders this and
/// never invents it.
struct LiveDeckState {
  bool hasTrack{false};
  bool isPlaying{false};
  double positionSec{0.0};
  double durationSec{0.0};
  float peakLeft{0.0F};  // linear 0..1+, post channel strip
  float peakRight{0.0F};
  bool hasStems{false};
  double playbackSpeed{1.0};  // what the deck actually plays at (a sync can change it behind the slider)
};

struct LiveEngineState {
  std::array<LiveDeckState, kDeckCount> decks{};
  float masterPeakLeft{0.0F};
  float masterPeakRight{0.0F};
  std::uint64_t droppedMessages{0};  // realtime messages lost to a full queue
  std::uint64_t noticeSerial{0};     // increases whenever the engine has something to tell the user
  std::string notice;                // e.g. why a sync was not possible
};

/// Implemented by the audio engine. Call from the UI thread only: reading consumes the engine's lock-free snapshot.
class ILiveEngineSource {
 public:
  virtual ~ILiveEngineSource() = default;
  [[nodiscard]] virtual LiveEngineState liveState() = 0;
};

enum class DeckLoadPhase : std::uint8_t { Empty = 0, Loading, Ready, Failed };

/// Progress of the neural stem separation of the track on a deck.
enum class StemPhase : std::uint8_t { None = 0, Queued, Running, Ready, Failed };

/// Where a LoadTrack request stands. `generation` increases on every change so a poller can tell "new waveform".
struct DeckLoadStatus {
  DeckLoadPhase phase{DeckLoadPhase::Empty};
  TrackId track{};
  std::uint64_t generation{0};
  std::string message;                          // the reason when phase == Failed
  std::shared_ptr<const WaveformData> waveform;  // set when phase == Ready and a waveform provider is installed
  StemPhase stemPhase{StemPhase::None};
  float stemProgress{0.0F};                     // 0..1 while stemPhase == Running
  std::string stemMessage;                      // the reason when stemPhase == Failed
};

class IDeckLoadSource {
 public:
  virtual ~IDeckLoadSource() = default;
  [[nodiscard]] virtual DeckLoadStatus loadStatus(DeckId deck) const = 0;
};

}  // namespace zyron::core
