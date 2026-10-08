// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/LLM/LocalLlmAdapter.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace zyron::ai {

namespace {

std::string toLower(const std::string& str) {
  std::string lower;
  lower.reserve(str.size());
  for (char c : str) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return lower;
}

}  // namespace

bool LocalLlmAdapter::containsDangerousTokens(const std::string& text) noexcept {
  const std::string lower = toLower(text);
  // Security guard against shell escapes, code injections or system calls (SPEC section 76)
  const char* dangerous[] = {
      "/bin/", "/etc/", "c:\\windows", "system32", "cmd.exe", "powershell",
      "exec(", "system(", "rm -rf", "del /f", "chmod ", "curl ", "wget ", "format c:"
  };

  for (const char* token : dangerous) {
    if (lower.find(token) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::optional<core::DeckId> LocalLlmAdapter::parseDeckLetter(char letter) noexcept {
  switch (std::toupper(static_cast<unsigned char>(letter))) {
    case 'A': return core::DeckId::A;
    case 'B': return core::DeckId::B;
    case 'C': return core::DeckId::C;
    case 'D': return core::DeckId::D;
  }
  return std::nullopt;
}

core::LlmCommandIntent LocalLlmAdapter::processInstruction(
    const std::string& prompt,
    const core::LlmPromptContext& context) const {
  core::LlmCommandIntent intent;
  intent.rawPrompt = prompt;

  if (containsDangerousTokens(prompt)) {
    intent.isValid = false;
    intent.validationError = "Security violation: Shell execution or system operations are strictly prohibited (SPEC section 76).";
    return intent;
  }

  const std::string lower = toLower(prompt);

  // 1. SPEC §60: "Сделай переход на что-нибудь тяжелее" / "transition heavier"
  if (lower.find("тяжелее") != std::string::npos || lower.find("heavier") != std::string::npos ||
      lower.find("more energy") != std::string::npos || lower.find("banger") != std::string::npos) {
    intent.intentType = "transition_heavier";
    intent.targetDeck = context.idleDeck;

    // Pick track from candidates with higher energy
    const core::TrackItem* best = nullptr;
    double targetEnergy = context.currentEnergy + 1.8;
    double bestDiff = 999.0;

    for (const auto& cand : context.candidateTracks) {
      if (cand.energy > context.currentEnergy) {
        double diff = std::abs(cand.energy - targetEnergy);
        if (diff < bestDiff) {
          bestDiff = diff;
          best = &cand;
        }
      }
    }

    if (best) {
      intent.generatedCommands.push_back(core::LoadTrack{context.idleDeck, core::TrackId{best->id}});
      intent.generatedCommands.push_back(core::SetEq{context.idleDeck, core::EqBand::Low, -60.0f});
      intent.generatedCommands.push_back(core::SetVolume{context.idleDeck, 1.0f});
      intent.generatedCommands.push_back(core::Play{context.idleDeck});

      std::ostringstream ss;
      ss << "Selected heavier track '" << best->title << "' (Energy " << best->energy
         << ") for transition from current Energy " << context.currentEnergy << " onto Deck "
         << (context.idleDeck == core::DeckId::A ? "A" : "B") << ".";
      intent.explanation = ss.str();
      intent.isValid = true;
    } else {
      intent.explanation = "No heavier track found in provided library candidates.";
      intent.isValid = false;
      intent.validationError = "No candidate track with higher energy available.";
    }
    return intent;
  }

  // 2. Kill / Cut Bass on deck
  if (lower.find("kill the bass") != std::string::npos || lower.find("cut bass") != std::string::npos ||
      lower.find("убери бас") != std::string::npos || lower.find("заглуши бас") != std::string::npos) {
    core::DeckId deck = context.activeDeck;
    if (lower.find("deck a") != std::string::npos || lower.find("деке a") != std::string::npos || lower.find("деке а") != std::string::npos) {
      deck = core::DeckId::A;
    } else if (lower.find("deck b") != std::string::npos || lower.find("деке b") != std::string::npos || lower.find("деке б") != std::string::npos) {
      deck = core::DeckId::B;
    }

    intent.intentType = "kill_bass";
    intent.targetDeck = deck;
    intent.generatedCommands.push_back(core::SetEq{deck, core::EqBand::Low, -60.0f});
    intent.explanation = "Low EQ band killed to -60 dB on target deck.";
    intent.isValid = true;
    return intent;
  }

  // 3. Stem mute / solo
  if (lower.find("mute vocal") != std::string::npos || lower.find("замути вокал") != std::string::npos ||
      lower.find("mute vocals") != std::string::npos) {
    core::DeckId deck = context.activeDeck;
    if (lower.find("deck b") != std::string::npos || lower.find("деке b") != std::string::npos) {
      deck = core::DeckId::B;
    }
    intent.intentType = "stem_mute";
    intent.targetDeck = deck;
    intent.generatedCommands.push_back(core::SetStemMute{deck, core::StemKind::Vocals, true});
    intent.explanation = "Muted vocal stem on deck.";
    intent.isValid = true;
    return intent;
  }

  // 4. Transport: play, pause, stop
  if (lower.find("play") != std::string::npos || lower.find("играй") != std::string::npos) {
    core::DeckId deck = (lower.find("deck b") != std::string::npos) ? core::DeckId::B : core::DeckId::A;
    intent.intentType = "play";
    intent.targetDeck = deck;
    intent.generatedCommands.push_back(core::Play{deck});
    intent.explanation = "Started deck playback.";
    intent.isValid = true;
    return intent;
  }

  if (lower.find("stop") != std::string::npos || lower.find("стоп") != std::string::npos ||
      lower.find("pause") != std::string::npos || lower.find("пауза") != std::string::npos) {
    intent.intentType = "pause";
    intent.generatedCommands.push_back(core::Pause{context.activeDeck});
    intent.explanation = "Paused active deck playback.";
    intent.isValid = true;
    return intent;
  }

  intent.isValid = false;
  intent.validationError = "Instruction could not be mapped to supported DJ Command intent.";
  return intent;
}

core::LlmCommandIntent LocalLlmAdapter::parseAndValidateJsonPlan(const std::string& jsonPlan) const {
  core::LlmCommandIntent intent;
  intent.rawPrompt = jsonPlan;

  if (containsDangerousTokens(jsonPlan)) {
    intent.isValid = false;
    intent.validationError = "Security violation: Shell execution or system operations are strictly prohibited (SPEC section 76).";
    return intent;
  }

  // Parse structured actions via regex matching JSON schema
  // Example schema: {"action": "SetEq", "deck": "A", "band": "Low", "db": -24.0}
  // Example schema: {"action": "SetVolume", "deck": "B", "linear": 0.8}
  // Example schema: {"action": "Play", "deck": "A"}
  // Example schema: {"action": "SetStemMute", "deck": "B", "stem": "Vocals", "muted": true}
  
  const std::regex actionRegex("\\{\\s*\"action\"\\s*:\\s*\"([^\"]+)\"([^}]+)\\}");
  auto actionsBegin = std::sregex_iterator(jsonPlan.begin(), jsonPlan.end(), actionRegex);
  auto actionsEnd = std::sregex_iterator();

  if (actionsBegin == actionsEnd) {
    intent.isValid = false;
    intent.validationError = "Invalid JSON schema: No valid action blocks found.";
    return intent;
  }

  for (auto it = actionsBegin; it != actionsEnd; ++it) {
    const std::string actionName = (*it)[1].str();
    const std::string actionBody = (*it)[2].str();

    // Deck extraction
    const std::regex deckRegex("\"deck\"\\s*:\\s*\"([A-Da-d])\"");
    std::smatch deckMatch;
    core::DeckId deck = core::DeckId::A;
    if (std::regex_search(actionBody, deckMatch, deckRegex)) {
      auto parsed = parseDeckLetter(deckMatch[1].str()[0]);
      if (parsed) deck = *parsed;
    }

    if (actionName == "Play") {
      intent.generatedCommands.push_back(core::Play{deck});
    } else if (actionName == "Pause") {
      intent.generatedCommands.push_back(core::Pause{deck});
    } else if (actionName == "SetVolume") {
      const std::regex valRegex("\"linear\"\\s*:\\s*([0-9\\.]+)");
      std::smatch valMatch;
      if (std::regex_search(actionBody, valMatch, valRegex)) {
        try {
          float vol = std::stof(valMatch[1].str());
          vol = std::clamp(vol, 0.0f, 1.0f);
          intent.generatedCommands.push_back(core::SetVolume{deck, vol});
        } catch (...) {}
      }
    } else if (actionName == "SetEq") {
      const std::regex bandRegex("\"band\"\\s*:\\s*\"(Low|Mid|High)\"");
      const std::regex dbRegex("\"db\"\\s*:\\s*(-?[0-9\\.]+)");
      std::smatch bandMatch, dbMatch;
      if (std::regex_search(actionBody, bandMatch, bandRegex) && std::regex_search(actionBody, dbMatch, dbRegex)) {
        core::EqBand band = core::EqBand::Low;
        const std::string bStr = bandMatch[1].str();
        if (bStr == "Mid") band = core::EqBand::Mid;
        else if (bStr == "High") band = core::EqBand::High;

        try {
          float db = std::stof(dbMatch[1].str());
          db = std::clamp(db, core::limits::kEqMinDb, core::limits::kEqMaxDb);
          intent.generatedCommands.push_back(core::SetEq{deck, band, db});
        } catch (...) {}
      }
    } else if (actionName == "SetStemMute") {
      const std::regex stemRegex("\"stem\"\\s*:\\s*\"(Vocals|Drums|Bass|Other)\"");
      const std::regex mutedRegex("\"muted\"\\s*:\\s*(true|false)");
      std::smatch stemMatch, mutedMatch;
      if (std::regex_search(actionBody, stemMatch, stemRegex) && std::regex_search(actionBody, mutedMatch, mutedRegex)) {
        core::StemKind stem = core::StemKind::Vocals;
        const std::string s = stemMatch[1].str();
        if (s == "Drums") stem = core::StemKind::Drums;
        else if (s == "Bass") stem = core::StemKind::Bass;
        else if (s == "Other") stem = core::StemKind::Other;

        bool muted = (mutedMatch[1].str() == "true");
        intent.generatedCommands.push_back(core::SetStemMute{deck, stem, muted});
      }
    } else {
      intent.isValid = false;
      intent.validationError = "Unrecognized or prohibited action name: '" + actionName + "'";
      return intent;
    }
  }

  intent.isValid = !intent.generatedCommands.empty();
  intent.intentType = "json_plan";
  intent.explanation = "Successfully validated " + std::to_string(intent.generatedCommands.size()) +
                       " structured Command API actions.";
  return intent;
}

}  // namespace zyron::ai
