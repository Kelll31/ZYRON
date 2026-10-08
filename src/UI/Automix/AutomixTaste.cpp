// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Automix/AutomixTaste.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace zyron::ui {

namespace {

constexpr int kStyleCount = static_cast<int>(core::TransitionStyle::ReverbOut) + 1;
constexpr int kMaxCount = 1000000;

juce::File tasteFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("ZYRON")
      .getChildFile("automix_taste.txt");
}

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  return first == std::string::npos ? std::string{} : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

}  // namespace

void recordChoice(TasteCounts& counts, core::TransitionStyle style) {
  int& count = counts[style];
  count = std::min(count + 1, kMaxCount);
}

std::vector<core::TransitionStyle> favouritesOf(const TasteCounts& counts, int threshold) {
  std::vector<core::TransitionStyle> favourites;
  for (const auto& [style, count] : counts) {  // std::map: enumeration order
    if (count >= threshold) {
      favourites.push_back(style);
    }
  }
  return favourites;
}

std::string formatTaste(const TasteCounts& counts) {
  std::ostringstream out;
  for (const auto& [style, count] : counts) {
    if (count > 0) {
      out << core::transitionStyleName(style) << '=' << count << '\n';
    }
  }
  return out.str();
}

TasteCounts parseTaste(const std::string& text) {
  TasteCounts counts;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos || eq == 0) {
      continue;
    }
    const std::string name = trim(line.substr(0, eq));
    const std::string number = trim(line.substr(eq + 1));
    char* end = nullptr;
    const long count = std::strtol(number.c_str(), &end, 10);
    if (number.empty() || end == nullptr || *end != '\0' || count <= 0) {
      continue;
    }
    for (int i = 0; i < kStyleCount; ++i) {
      const auto style = static_cast<core::TransitionStyle>(i);
      if (name == core::transitionStyleName(style)) {
        counts[style] = static_cast<int>(std::min<long>(count, kMaxCount));
      }
    }
  }
  return counts;
}

TasteCounts loadTaste() {
  const auto file = tasteFile();
  return parseTaste(file.existsAsFile() ? file.loadFileAsString().toStdString() : std::string{});
}

void saveTaste(const TasteCounts& counts) {
  const auto file = tasteFile();
  if (file.getParentDirectory().createDirectory().wasOk()) {
    (void)file.replaceWithText(juce::String(formatTaste(counts)));  // best effort
  }
}

}  // namespace zyron::ui
