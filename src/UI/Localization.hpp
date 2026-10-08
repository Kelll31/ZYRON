// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

#include <string_view>
#include <utility>
#include <vector>

namespace zyron::ui::i18n {

enum class Language { English, Russian };

/// The Russian table: {English text as written in the code, Russian text}. The English text is the key, so a string
/// without an entry simply stays English. Mark every user-visible literal with TRANS("...") (juce_core).
[[nodiscard]] const std::vector<std::pair<const char*, const char*>>& russianStrings();

/// Translates a message produced outside the UI (engine notices, Automix status). An exact table entry wins; an entry
/// with "%s" placeholders (e.g. "Mixing into %s") matches messages that vary only in that part.
[[nodiscard]] juce::String translateMessage(std::string_view message);

/// Switches the UI texts. Components read texts when they are built, so the window rebuilds its content afterwards.
void apply(Language language);
[[nodiscard]] Language current() noexcept;

/// The language saved by the user; the first run follows the system language (Russian system -> Russian).
[[nodiscard]] Language loadSaved();
void save(Language language);

}  // namespace zyron::ui::i18n
