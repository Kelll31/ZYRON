// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Contextual DJ environment snapshot passed to the local LLM adapter (SPEC sections 59, 60).
struct LlmPromptContext {
  DeckId activeDeck{DeckId::A};
  DeckId idleDeck{DeckId::B};
  double currentBpm{174.0};
  std::string currentKey{"8A"};
  double currentEnergy{6.4};
  std::string currentGenre{"Drum & Bass"};
  std::vector<TrackItem> candidateTracks;
};

/// Structured and validated action outcome from LLM interpretation.
struct LlmCommandIntent {
  std::string rawPrompt;
  std::string intentType;               // e.g. "transition_heavier", "kill_bass", "stem_mute", "set_volume", "play", "stop"
  std::optional<DeckId> targetDeck;
  std::vector<Command> generatedCommands;
  std::string explanation;
  bool isValid{false};
  std::string validationError;
};

/// Abstract interface for local LLM natural language instruction processing (SPEC sections 59, 60, 76, ROADMAP P8-04).
class ILocalLlmAdapter {
 public:
  virtual ~ILocalLlmAdapter() = default;

  /// Interprets a natural language DJ prompt into validated Command API instructions.
  [[nodiscard]] virtual LlmCommandIntent processInstruction(
      const std::string& prompt,
      const LlmPromptContext& context) const = 0;

  /// Validates a raw JSON tool-calling response string against Command schema and returns typed commands.
  [[nodiscard]] virtual LlmCommandIntent parseAndValidateJsonPlan(
      const std::string& jsonPlan) const = 0;
};

}  // namespace zyron::core
