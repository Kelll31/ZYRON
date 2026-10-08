// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

#include "Core/AI/AutonomousDjTypes.hpp"

namespace zyron::ui {

/// Sizes of the user-resizable blocks, in pixels; 0 means "use the default" (nothing saved yet).
struct LayoutSizes {
  int bottomHeight{0};          // library / automix area under the decks
  int automixSettingsWidth{0};  // settings panel to the right of the automix queue
  int waveformHeight{0};        // global waveform strip at the top (one value for both layouts)
  int mixerWidth2{0};           // mixer column in the 2-deck layout
  int mixerWidth4{0};           // mixer column in the 4-deck layout
  int deckSplitPercent{0};      // share of the deck width given to the left deck column (10..90)
};

// Text formats: simple "key=value" lines. The parsers are defensive (unknown keys, junk lines and out-of-range numbers
// are ignored) because the files are user-editable and may come from another version.
[[nodiscard]] std::string formatMixProfile(const core::MixProfile& profile);
[[nodiscard]] core::MixProfile parseMixProfile(const std::string& text);
[[nodiscard]] std::string formatLayoutSizes(const LayoutSizes& sizes);
[[nodiscard]] LayoutSizes parseLayoutSizes(const std::string& text);

/// Files in <user application data>/ZYRON (automix.txt, layout.txt). Load returns the defaults when the file is
/// missing or unreadable; save is best effort (a failure only means the choice is not remembered).
[[nodiscard]] core::MixProfile loadMixProfile();
void saveMixProfile(const core::MixProfile& profile);
[[nodiscard]] LayoutSizes loadLayoutSizes();
void saveLayoutSizes(const LayoutSizes& sizes);

}  // namespace zyron::ui
