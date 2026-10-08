// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Core/AI/LlmAdapterTypes.hpp"

namespace zyron::ai {

/// Local LLM adapter with strict Command API validation (SPEC sections 59, 60, 76, ROADMAP P8-04).
class LocalLlmAdapter final : public core::ILocalLlmAdapter {
 public:
  LocalLlmAdapter() = default;
  ~LocalLlmAdapter() override = default;

  [[nodiscard]] core::LlmCommandIntent processInstruction(
      const std::string& prompt,
      const core::LlmPromptContext& context) const override;

  [[nodiscard]] core::LlmCommandIntent parseAndValidateJsonPlan(
      const std::string& jsonPlan) const override;

 private:
  [[nodiscard]] static bool containsDangerousTokens(const std::string& text) noexcept;
  [[nodiscard]] static std::optional<core::DeckId> parseDeckLetter(char letter) noexcept;
};

}  // namespace zyron::ai
