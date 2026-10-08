// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <string>

namespace zyron::core {

/// Who issued a command. User input (Ui/Midi) always outranks AI automation on the same target; the AI scheduler
/// yields when it sees a user-origin command there (ARCHITECTURE section 5, SPEC section 8).
struct CommandOrigin {
  enum class Kind : std::uint8_t { Ui, Midi, Ai, Script };

  Kind kind{Kind::Ui};
  std::string sourceId;  // component id, MIDI mapping id, planner run id... for logs and diagnostics only
};

enum class CommandErrorCode : std::uint8_t {
  InvalidDeck,
  InvalidTrack,
  InvalidBand,
  InvalidStem,
  InvalidName,  // device/API name too long or containing control characters
  NotFinite,
  OutOfRange,
  NoTrackLoaded,
  Reentrant,  // submit() was called from inside a CommandSink callback; refused to avoid a deadlock
};

struct CommandError {
  CommandErrorCode code{CommandErrorCode::OutOfRange};
  std::string message;  // human-readable, English; the UI maps `code` to localised text
};

}  // namespace zyron::core
