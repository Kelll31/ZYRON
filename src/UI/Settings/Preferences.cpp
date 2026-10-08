// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Settings/Preferences.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <map>
#include <sstream>

namespace zyron::ui {

namespace {

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  return first == std::string::npos ? std::string{} : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

bool readBool(const std::map<std::string, std::string>& values, const char* key, bool fallback) {
  const auto it = values.find(key);
  if (it == values.end()) {
    return fallback;
  }
  if (it->second == "1" || it->second == "true") {
    return true;
  }
  return (it->second == "0" || it->second == "false") ? false : fallback;
}

juce::File preferencesFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("ZYRON")
      .getChildFile("preferences.txt");
}

}  // namespace

std::optional<float> loudnessTrimDb(double lufs) {
  if (!(lufs < 0.0)) {
    return std::nullopt;
  }
  return static_cast<float>(std::clamp(kLoudnessTargetLufs - lufs, -kLoudnessTrimLimitDb, kLoudnessTrimLimitDb));
}

std::string formatPreferences(const Preferences& p) {
  std::ostringstream out;
  out << "autoLoudness=" << p.autoLoudness << '\n' << "glue=" << p.glue << '\n' << "limiter=" << p.limiter << '\n' << "autoStems=" << p.autoStems << '\n';
  return out.str();
}

Preferences parsePreferences(const std::string& text) {
  std::map<std::string, std::string> values;
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
  Preferences p;
  p.autoLoudness = readBool(values, "autoLoudness", p.autoLoudness);
  p.autoStems = readBool(values, "autoStems", p.autoStems);
  p.glue = readBool(values, "glue", p.glue);
  p.limiter = readBool(values, "limiter", p.limiter);
  return p;
}

Preferences loadPreferences() {
  const auto file = preferencesFile();
  return parsePreferences(file.existsAsFile() ? file.loadFileAsString().toStdString() : std::string{});
}

void savePreferences(const Preferences& preferences) {
  const auto file = preferencesFile();
  if (file.getParentDirectory().createDirectory().wasOk()) {
    (void)file.replaceWithText(juce::String(formatPreferences(preferences)));  // best effort
  }
}

}  // namespace zyron::ui
