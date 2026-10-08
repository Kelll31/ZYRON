// SPDX-License-Identifier: AGPL-3.0-only
#include "MIDI/MidiMapper.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace zyron::midi {

namespace {

std::string messageTypeToString(core::MidiMessageType t) {
  switch (t) {
    case core::MidiMessageType::NoteOn: return "NoteOn";
    case core::MidiMessageType::NoteOff: return "NoteOff";
    case core::MidiMessageType::ControlChange: return "ControlChange";
    case core::MidiMessageType::PitchBend: return "PitchBend";
  }
  return "ControlChange";
}

core::MidiMessageType stringToMessageType(std::string_view s) {
  if (s == "NoteOn") return core::MidiMessageType::NoteOn;
  if (s == "NoteOff") return core::MidiMessageType::NoteOff;
  if (s == "PitchBend") return core::MidiMessageType::PitchBend;
  return core::MidiMessageType::ControlChange;
}

std::string deckToString(std::optional<core::DeckId> d) {
  if (!d) return "";
  switch (*d) {
    case core::DeckId::A: return "A";
    case core::DeckId::B: return "B";
    case core::DeckId::C: return "C";
    case core::DeckId::D: return "D";
  }
  return "A";
}

std::optional<core::DeckId> stringToDeck(std::string_view s) {
  if (s == "A") return core::DeckId::A;
  if (s == "B") return core::DeckId::B;
  if (s == "C") return core::DeckId::C;
  if (s == "D") return core::DeckId::D;
  return std::nullopt;
}

std::string eqBandToString(std::optional<core::EqBand> b) {
  if (!b) return "";
  switch (*b) {
    case core::EqBand::Low: return "Low";
    case core::EqBand::Mid: return "Mid";
    case core::EqBand::High: return "High";
  }
  return "Low";
}

std::optional<core::EqBand> stringToEqBand(std::string_view s) {
  if (s == "Low") return core::EqBand::Low;
  if (s == "Mid") return core::EqBand::Mid;
  if (s == "High") return core::EqBand::High;
  return std::nullopt;
}

std::string stemToString(std::optional<core::StemKind> s) {
  if (!s) return "";
  switch (*s) {
    case core::StemKind::Drums: return "Drums";
    case core::StemKind::Bass: return "Bass";
    case core::StemKind::Vocals: return "Vocals";
    case core::StemKind::Other: return "Other";
  }
  return "Vocals";
}

std::optional<core::StemKind> stringToStem(std::string_view s) {
  if (s == "Drums") return core::StemKind::Drums;
  if (s == "Bass") return core::StemKind::Bass;
  if (s == "Vocals") return core::StemKind::Vocals;
  if (s == "Other") return core::StemKind::Other;
  return std::nullopt;
}

}  // namespace

MidiMapper::MidiMapper(core::CommandBus& commandBus)
    : commandBus_(&commandBus), profile_(createDefault2DeckProfile()) {}

std::optional<core::Command> MidiMapper::translateEvent(const core::MidiEvent& event) const {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto& entry : profile_.mappings) {
    if (entry.channel == event.channel && entry.number == event.number && entry.messageType == event.type) {
      float norm = event.normalizedValue();
      if (entry.invert) {
        norm = 1.0f - norm;
      }
      const float param = entry.minParamValue + norm * (entry.maxParamValue - entry.minParamValue);
      const auto deck = entry.deck.value_or(core::DeckId::A);

      if (entry.targetCommandName == "PLAY") {
        if (event.type == core::MidiMessageType::NoteOn && event.value > 0) {
          return core::Play{deck};
        }
        if (event.type == core::MidiMessageType::ControlChange && event.value > 63) {
          return core::Play{deck};
        }
      } else if (entry.targetCommandName == "PAUSE") {
        if (event.value > 0) return core::Pause{deck};
      } else if (entry.targetCommandName == "CUE") {
        if (event.value > 0) return core::Cue{deck};
      } else if (entry.targetCommandName == "SET_VOLUME") {
        return core::SetVolume{deck, std::clamp(param, 0.0f, 1.0f)};
      } else if (entry.targetCommandName == "SET_GAIN") {
        return core::SetGain{deck, std::clamp(param, core::limits::kGainMinDb, core::limits::kGainMaxDb)};
      } else if (entry.targetCommandName == "SET_EQ") {
        const auto band = entry.eqBand.value_or(core::EqBand::Low);
        return core::SetEq{deck, band, std::clamp(param, core::limits::kEqMinDb, core::limits::kEqMaxDb)};
      } else if (entry.targetCommandName == "SET_CROSSFADER") {
        return core::SetCrossfader{std::clamp(param, -1.0f, 1.0f)};
      } else if (entry.targetCommandName == "SET_STEM_VOLUME") {
        const auto stem = entry.stem.value_or(core::StemKind::Vocals);
        return core::SetStemVolume{deck, stem, std::clamp(param, 0.0f, 1.0f)};
      } else if (entry.targetCommandName == "SET_STEM_MUTE") {
        const auto stem = entry.stem.value_or(core::StemKind::Vocals);
        return core::SetStemMute{deck, stem, (param > 0.5f)};
      } else if (entry.targetCommandName == "SET_STEM_SOLO") {
        const auto stem = entry.stem.value_or(core::StemKind::Vocals);
        return core::SetStemSolo{deck, stem, (param > 0.5f)};
      } else if (entry.targetCommandName == "SET_DECK_CUE") {
        return core::SetDeckCue{deck, (param > 0.5f)};
      } else if (entry.targetCommandName == "SET_MASTER_GAIN") {
        return core::SetMasterGain{param};
      }
    }
  }

  return std::nullopt;
}

bool MidiMapper::processAndSubmit(const core::MidiEvent& event) {
  const auto cmd = translateEvent(event);
  if (!cmd.has_value() || !commandBus_) {
    return false;
  }

  core::CommandOrigin origin;
  origin.kind = core::CommandOrigin::Kind::Midi;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    origin.sourceId = profile_.controllerName;
  }

  const auto err = commandBus_->submit(*cmd, origin);
  return !err.has_value();
}

std::string MidiMapper::exportProfileJson(const core::MidiProfile& profile) {
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"version\": " << profile.version << ",\n";
  ss << "  \"controllerName\": \"" << profile.controllerName << "\",\n";
  ss << "  \"author\": \"" << profile.author << "\",\n";
  ss << "  \"description\": \"" << profile.description << "\",\n";
  ss << "  \"mappings\": [\n";

  for (std::size_t i = 0; i < profile.mappings.size(); ++i) {
    const auto& m = profile.mappings[i];
    ss << "    {\n";
    ss << "      \"messageType\": \"" << messageTypeToString(m.messageType) << "\",\n";
    ss << "      \"channel\": " << m.channel << ",\n";
    ss << "      \"number\": " << m.number << ",\n";
    ss << "      \"targetCommand\": \"" << m.targetCommandName << "\"";

    if (m.deck.has_value()) {
      ss << ",\n      \"deck\": \"" << deckToString(m.deck) << "\"";
    }
    if (m.eqBand.has_value()) {
      ss << ",\n      \"eqBand\": \"" << eqBandToString(m.eqBand) << "\"";
    }
    if (m.stem.has_value()) {
      ss << ",\n      \"stem\": \"" << stemToString(m.stem) << "\"";
    }
    ss << ",\n      \"min\": " << std::fixed << std::setprecision(2) << m.minParamValue;
    ss << ",\n      \"max\": " << std::fixed << std::setprecision(2) << m.maxParamValue;
    ss << "\n    }" << (i + 1 < profile.mappings.size() ? "," : "") << "\n";
  }

  ss << "  ]\n";
  ss << "}\n";
  return ss.str();
}

std::optional<core::MidiProfile> MidiMapper::parseProfileJson(std::string_view json) {
  core::MidiProfile profile;
  const std::string text(json);

  const std::regex verRegex("\"version\"\\s*:\\s*([0-9]+)");
  std::smatch verMatch;
  if (std::regex_search(text, verMatch, verRegex)) {
    try { profile.version = std::stoi(verMatch[1].str()); } catch (...) {}
  }

  const std::regex nameRegex("\"controllerName\"\\s*:\\s*\"([^\"]+)\"");
  std::smatch nameMatch;
  if (std::regex_search(text, nameMatch, nameRegex)) {
    profile.controllerName = nameMatch[1].str();
  }

  const std::regex authorRegex("\"author\"\\s*:\\s*\"([^\"]+)\"");
  std::smatch authorMatch;
  if (std::regex_search(text, authorMatch, authorRegex)) {
    profile.author = authorMatch[1].str();
  }

  // Parse mappings
  const std::regex entryRegex("\\{\\s*\"messageType\"\\s*:\\s*\"([^\"]+)\"([^}]+)\\}");
  auto itBegin = std::sregex_iterator(text.begin(), text.end(), entryRegex);
  auto itEnd = std::sregex_iterator();

  for (auto it = itBegin; it != itEnd; ++it) {
    core::MidiMappingEntry entry;
    entry.messageType = stringToMessageType((*it)[1].str());
    const std::string body = (*it)[2].str();

    const std::regex chRegex("\"channel\"\\s*:\\s*([0-9]+)");
    std::smatch chMatch;
    if (std::regex_search(body, chMatch, chRegex)) {
      try { entry.channel = std::stoi(chMatch[1].str()); } catch (...) {}
    }

    const std::regex numRegex("\"number\"\\s*:\\s*([0-9]+)");
    std::smatch numMatch;
    if (std::regex_search(body, numMatch, numRegex)) {
      try { entry.number = std::stoi(numMatch[1].str()); } catch (...) {}
    }

    const std::regex cmdRegex("\"targetCommand\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch cmdMatch;
    if (std::regex_search(body, cmdMatch, cmdRegex)) {
      entry.targetCommandName = cmdMatch[1].str();
    }

    const std::regex deckRegex("\"deck\"\\s*:\\s*\"([A-Da-d])\"");
    std::smatch deckMatch;
    if (std::regex_search(body, deckMatch, deckRegex)) {
      entry.deck = stringToDeck(deckMatch[1].str());
    }

    const std::regex bandRegex("\"eqBand\"\\s*:\\s*\"(Low|Mid|High)\"");
    std::smatch bandMatch;
    if (std::regex_search(body, bandMatch, bandRegex)) {
      entry.eqBand = stringToEqBand(bandMatch[1].str());
    }

    const std::regex stemRegex("\"stem\"\\s*:\\s*\"(Vocals|Drums|Bass|Other)\"");
    std::smatch stemMatch;
    if (std::regex_search(body, stemMatch, stemRegex)) {
      entry.stem = stringToStem(stemMatch[1].str());
    }

    const std::regex minRegex("\"min\"\\s*:\\s*(-?[0-9\\.]+)");
    std::smatch minMatch;
    if (std::regex_search(body, minMatch, minRegex)) {
      try { entry.minParamValue = std::stof(minMatch[1].str()); } catch (...) {}
    }

    const std::regex maxRegex("\"max\"\\s*:\\s*(-?[0-9\\.]+)");
    std::smatch maxMatch;
    if (std::regex_search(body, maxMatch, maxRegex)) {
      try { entry.maxParamValue = std::stof(maxMatch[1].str()); } catch (...) {}
    }

    profile.mappings.push_back(std::move(entry));
  }

  if (profile.mappings.empty()) {
    return std::nullopt;
  }
  return profile;
}

bool MidiMapper::loadProfileFromFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) return false;
  std::stringstream ss;
  ss << file.rdbuf();
  auto parsed = parseProfileJson(ss.str());
  if (!parsed) return false;
  setProfile(std::move(*parsed));
  return true;
}

bool MidiMapper::saveProfileToFile(const core::MidiProfile& profile, const std::filesystem::path& path) {
  std::ofstream file(path);
  if (!file.is_open()) return false;
  file << exportProfileJson(profile);
  return true;
}

core::MidiProfile MidiMapper::createDefault2DeckProfile() {
  core::MidiProfile p;
  p.version = 1;
  p.controllerName = "Standard 2-Deck MIDI Console";
  p.author = "ZYRON Factory";
  p.description = "Default mappings for Deck A/B transport, EQ knobs, channel faders and crossfader.";

  // Deck A Transport
  p.mappings.push_back({ "deck_a_play", core::MidiMessageType::NoteOn, 1, 11, core::MidiMappingMode::Absolute, "PLAY", core::DeckId::A, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ "deck_a_pause", core::MidiMessageType::NoteOn, 1, 12, core::MidiMappingMode::Absolute, "PAUSE", core::DeckId::A, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ "deck_a_cue", core::MidiMessageType::NoteOn, 1, 13, core::MidiMappingMode::Absolute, "CUE", core::DeckId::A, std::nullopt, std::nullopt, 0.0f, 1.0f, false });

  // Deck B Transport
  p.mappings.push_back({ "deck_b_play", core::MidiMessageType::NoteOn, 2, 11, core::MidiMappingMode::Absolute, "PLAY", core::DeckId::B, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ "deck_b_pause", core::MidiMessageType::NoteOn, 2, 12, core::MidiMappingMode::Absolute, "PAUSE", core::DeckId::B, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ "deck_b_cue", core::MidiMessageType::NoteOn, 2, 13, core::MidiMappingMode::Absolute, "CUE", core::DeckId::B, std::nullopt, std::nullopt, 0.0f, 1.0f, false });

  // Mixer Faders
  p.mappings.push_back({ "deck_a_volume", core::MidiMessageType::ControlChange, 1, 19, core::MidiMappingMode::Absolute, "SET_VOLUME", core::DeckId::A, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ "deck_b_volume", core::MidiMessageType::ControlChange, 2, 19, core::MidiMappingMode::Absolute, "SET_VOLUME", core::DeckId::B, std::nullopt, std::nullopt, 0.0f, 1.0f, false });
  p.mappings.push_back({ "crossfader", core::MidiMessageType::ControlChange, 1, 31, core::MidiMappingMode::Absolute, "SET_CROSSFADER", std::nullopt, std::nullopt, std::nullopt, -1.0f, 1.0f, false });

  // Deck A EQs
  p.mappings.push_back({ "deck_a_eq_low", core::MidiMessageType::ControlChange, 1, 23, core::MidiMappingMode::Absolute, "SET_EQ", core::DeckId::A, core::EqBand::Low, std::nullopt, -60.0f, 12.0f, false });
  p.mappings.push_back({ "deck_a_eq_mid", core::MidiMessageType::ControlChange, 1, 22, core::MidiMappingMode::Absolute, "SET_EQ", core::DeckId::A, core::EqBand::Mid, std::nullopt, -24.0f, 12.0f, false });
  p.mappings.push_back({ "deck_a_eq_high", core::MidiMessageType::ControlChange, 1, 21, core::MidiMappingMode::Absolute, "SET_EQ", core::DeckId::A, core::EqBand::High, std::nullopt, -24.0f, 12.0f, false });

  // Deck B EQs
  p.mappings.push_back({ "deck_b_eq_low", core::MidiMessageType::ControlChange, 2, 23, core::MidiMappingMode::Absolute, "SET_EQ", core::DeckId::B, core::EqBand::Low, std::nullopt, -60.0f, 12.0f, false });
  p.mappings.push_back({ "deck_b_eq_mid", core::MidiMessageType::ControlChange, 2, 22, core::MidiMappingMode::Absolute, "SET_EQ", core::DeckId::B, core::EqBand::Mid, std::nullopt, -24.0f, 12.0f, false });
  p.mappings.push_back({ "deck_b_eq_high", core::MidiMessageType::ControlChange, 2, 21, core::MidiMappingMode::Absolute, "SET_EQ", core::DeckId::B, core::EqBand::High, std::nullopt, -24.0f, 12.0f, false });

  return p;
}

}  // namespace zyron::midi
