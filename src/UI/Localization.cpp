// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Localization.hpp"

#include <atomic>
#include <vector>

namespace zyron::ui::i18n {

namespace {

std::atomic<Language> gCurrent{Language::English};

/// A table entry with "%s" placeholders: `parts` are the literal pieces around them (placeholders + 1 of them).
struct MessageTemplate {
  juce::StringArray parts;
  juce::String english;
};
std::vector<MessageTemplate> gTemplates;  // message thread only (apply() and translateMessage())

juce::String escaped(const juce::String& text) {
  return text.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n");
}

juce::StringArray splitAtPlaceholders(const juce::String& key) {
  juce::StringArray parts;
  int from = 0;
  for (int at = key.indexOf(from, "%s"); at >= 0; at = key.indexOf(from, "%s")) {
    parts.add(key.substring(from, at));
    from = at + 2;
  }
  parts.add(key.substring(from));
  return parts;
}

/// Finds the placeholder values of `text` in `t`; empty when the message does not fit the template.
juce::StringArray matchTemplate(const MessageTemplate& t, const juce::String& text) {
  const auto& parts = t.parts;
  const int last = parts.size() - 1;
  if (!text.startsWith(parts[0]) || !text.endsWith(parts[last]) ||
      text.length() < parts[0].length() + parts[last].length()) {
    return {};
  }
  juce::StringArray values;
  int pos = parts[0].length();
  const int end = text.length() - parts[last].length();
  for (int i = 1; i < last; ++i) {  // the literal pieces between two placeholders
    const int at = text.indexOf(pos, parts[i]);
    if (at < 0 || at + parts[i].length() > end) {
      return {};
    }
    values.add(text.substring(pos, at));
    pos = at + parts[i].length();
  }
  values.add(text.substring(pos, end));
  return values;
}

juce::File settingsFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("ZYRON")
      .getChildFile("language.txt");
}

}  // namespace

void apply(Language language) {
  gCurrent.store(language);
  gTemplates.clear();
  if (language == Language::English) {
    juce::LocalisedStrings::setCurrentMappings(nullptr);  // the keys are the English texts
    return;
  }
  juce::String content = "language: Russian\n";
  for (const auto& [english, russian] : russianStrings()) {
    const juce::String key = juce::String::fromUTF8(english);
    if (key.contains("%s")) {
      gTemplates.push_back({splitAtPlaceholders(key), key});
    }
    content << "\"" << escaped(juce::String::fromUTF8(english)) << "\" = \"" << escaped(juce::String::fromUTF8(russian))
            << "\"\n";
  }
  juce::LocalisedStrings::setCurrentMappings(new juce::LocalisedStrings(content, false));
}

juce::String translateMessage(std::string_view message) {
  const juce::String text = juce::String::fromUTF8(message.data(), static_cast<int>(message.size()));
  if (gCurrent.load() == Language::English || text.isEmpty()) {
    return text;
  }
  const juce::String exact = juce::translate(text);
  if (exact != text) {
    return exact;
  }
  for (const auto& t : gTemplates) {
    const juce::StringArray values = matchTemplate(t, text);
    if (values.isEmpty()) {
      continue;
    }
    const juce::String russian = juce::translate(t.english);
    juce::String result;
    int valueIndex = 0;
    for (int i = 0; i < russian.length(); ++i) {
      if (russian[i] == '%' && russian[i + 1] == 's' && valueIndex < values.size()) {
        result << juce::translate(values[valueIndex++]);  // a value may itself be a known word ("Bass Swap")
        ++i;
      } else {
        result << russian.substring(i, i + 1);
      }
    }
    return result;
  }
  return text;
}

Language current() noexcept {
  return gCurrent.load();
}

Language loadSaved() {
  const auto file = settingsFile();
  if (file.existsAsFile()) {
    return file.loadFileAsString().trim().equalsIgnoreCase("ru") ? Language::Russian : Language::English;
  }
  return juce::SystemStats::getUserLanguage().startsWithIgnoreCase("ru") ? Language::Russian : Language::English;
}

void save(Language language) {
  const auto file = settingsFile();
  if (file.getParentDirectory().createDirectory().wasOk()) {
    (void)file.replaceWithText(language == Language::Russian ? "ru" : "en");
  }
}

}  // namespace zyron::ui::i18n
