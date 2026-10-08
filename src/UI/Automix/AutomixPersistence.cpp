// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Automix/AutomixPersistence.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace zyron::ui {

namespace {

using KeyValues = std::map<std::string, std::string>;

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

KeyValues parseKeyValues(const std::string& text) {
  KeyValues values;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos || eq == 0) {
      continue;
    }
    const std::string key = trim(line.substr(0, eq));
    if (!key.empty() && key.front() != '#') {
      values[key] = trim(line.substr(eq + 1));
    }
  }
  return values;
}

bool readBool(const KeyValues& values, const char* key, bool fallback) {
  const auto it = values.find(key);
  if (it == values.end()) {
    return fallback;
  }
  if (it->second == "1" || it->second == "true") {
    return true;
  }
  if (it->second == "0" || it->second == "false") {
    return false;
  }
  return fallback;
}

int readInt(const KeyValues& values, const char* key, int fallback) {
  const auto it = values.find(key);
  if (it == values.end() || it->second.empty()) {
    return fallback;
  }
  char* end = nullptr;
  const long v = std::strtol(it->second.c_str(), &end, 10);
  return (end != nullptr && *end == '\0') ? static_cast<int>(std::clamp(v, -100000L, 100000L)) : fallback;
}

const char* modeName(core::MixMode mode) {
  switch (mode) {
    case core::MixMode::Smooth: return "smooth";
    case core::MixMode::Club: return "club";
    case core::MixMode::Battle: return "battle";
    case core::MixMode::Custom: return "custom";
  }
  return "club";
}

core::MixMode readMode(const KeyValues& values) {
  const auto it = values.find("mode");
  if (it != values.end()) {
    for (const auto mode : {core::MixMode::Smooth, core::MixMode::Club, core::MixMode::Battle, core::MixMode::Custom}) {
      if (it->second == modeName(mode)) {
        return mode;
      }
    }
  }
  return core::MixMode::Club;
}

/// The transition length is one of 16 / 32 / 64 beats in the UI: snap anything else to the nearest.
double snapBeats(int beats) {
  if (beats <= 0) {
    return 32.0;
  }
  double best = 32.0;
  int bestDistance = 1 << 30;
  for (const int option : {16, 32, 64}) {
    if (std::abs(option - beats) < bestDistance) {
      bestDistance = std::abs(option - beats);
      best = option;
    }
  }
  return best;
}

juce::File settingsFile(const char* name) {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("ZYRON")
      .getChildFile(name);
}

std::string readFile(const juce::File& file) {
  return file.existsAsFile() ? file.loadFileAsString().toStdString() : std::string{};
}

void writeFile(const juce::File& file, const std::string& text) {
  if (file.getParentDirectory().createDirectory().wasOk()) {
    file.replaceWithText(juce::String(text));  // best effort: failing only loses the remembered choice
  }
}

}  // namespace

std::string formatMixProfile(const core::MixProfile& p) {
  std::ostringstream out;
  out << "mode=" << modeName(p.mode) << '\n'
      << "blend=" << p.blend << '\n'
      << "filter=" << p.filter << '\n'
      << "loopRoll=" << p.loopRoll << '\n'
      << "brake=" << p.brake << '\n'
      << "scratch=" << p.scratch << '\n'
      << "cut=" << p.cut << '\n'
      << "beatLoop=" << p.beatLoop << '\n'
      << "doubleDrop=" << p.doubleDrop << '\n'
      << "stems=" << p.stems << '\n'
      << "fxOut=" << p.fxOut << '\n'
      << "fxHits=" << p.fxHits << '\n'
      << "transitionBeats=" << static_cast<int>(std::lround(p.transitionBeats)) << '\n';
  return out.str();
}

core::MixProfile parseMixProfile(const std::string& text) {
  const auto values = parseKeyValues(text);
  core::MixProfile p = core::MixProfile::preset(readMode(values));  // fields missing from the file keep the preset
  p.blend = readBool(values, "blend", p.blend);
  p.filter = readBool(values, "filter", p.filter);
  p.loopRoll = readBool(values, "loopRoll", p.loopRoll);
  p.brake = readBool(values, "brake", p.brake);
  p.scratch = readBool(values, "scratch", p.scratch);
  p.cut = readBool(values, "cut", p.cut);
  p.beatLoop = readBool(values, "beatLoop", p.beatLoop);
  p.doubleDrop = readBool(values, "doubleDrop", p.doubleDrop);  // old files lack these four: the preset's values stay
  p.stems = readBool(values, "stems", p.stems);
  p.fxOut = readBool(values, "fxOut", p.fxOut);
  p.fxHits = readBool(values, "fxHits", p.fxHits);
  p.transitionBeats = snapBeats(readInt(values, "transitionBeats", static_cast<int>(p.transitionBeats)));
  return p;
}

std::string formatLayoutSizes(const LayoutSizes& s) {
  std::ostringstream out;
  out << "bottomHeight=" << s.bottomHeight << '\n' << "automixSettingsWidth=" << s.automixSettingsWidth << '\n'
      << "waveformHeight=" << s.waveformHeight << '\n' << "mixerWidth2=" << s.mixerWidth2 << '\n'
      << "mixerWidth4=" << s.mixerWidth4 << '\n' << "deckSplitPercent=" << s.deckSplitPercent << '\n';
  return out.str();
}

LayoutSizes parseLayoutSizes(const std::string& text) {
  const auto values = parseKeyValues(text);
  LayoutSizes s;
  s.bottomHeight = std::max(0, readInt(values, "bottomHeight", 0));
  s.automixSettingsWidth = std::max(0, readInt(values, "automixSettingsWidth", 0));
  s.waveformHeight = std::max(0, readInt(values, "waveformHeight", 0));
  s.mixerWidth2 = std::max(0, readInt(values, "mixerWidth2", 0));
  s.mixerWidth4 = std::max(0, readInt(values, "mixerWidth4", 0));
  const int split = readInt(values, "deckSplitPercent", 0);
  s.deckSplitPercent = (split >= 10 && split <= 90) ? split : 0;  // anything else: the even default
  return s;
}

core::MixProfile loadMixProfile() {
  return parseMixProfile(readFile(settingsFile("automix.txt")));
}

void saveMixProfile(const core::MixProfile& profile) {
  writeFile(settingsFile("automix.txt"), formatMixProfile(profile));
}

LayoutSizes loadLayoutSizes() {
  return parseLayoutSizes(readFile(settingsFile("layout.txt")));
}

void saveLayoutSizes(const LayoutSizes& sizes) {
  writeFile(settingsFile("layout.txt"), formatLayoutSizes(sizes));
}

}  // namespace zyron::ui
