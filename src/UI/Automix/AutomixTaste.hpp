// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <map>
#include <string>
#include <vector>

#include "Core/AI/TransitionTypes.hpp"

namespace zyron::ui {

/// How often the DJ picked each transition style by hand in the queue menu. Styles picked at least
/// `kFavouriteThreshold` times become favourites: the Automix uses them more often (MixProfile::favourites).
using TasteCounts = std::map<core::TransitionStyle, int>;

inline constexpr int kFavouriteThreshold = 3;

void recordChoice(TasteCounts& counts, core::TransitionStyle style);
/// Each style at the threshold or above once, in the order of the enumeration.
[[nodiscard]] std::vector<core::TransitionStyle> favouritesOf(const TasteCounts& counts,
                                                              int threshold = kFavouriteThreshold);

[[nodiscard]] std::string formatTaste(const TasteCounts& counts);
/// Defensive: unknown styles, junk and non-positive counts are ignored.
[[nodiscard]] TasteCounts parseTaste(const std::string& text);

/// automix_taste.txt next to automix.txt.
[[nodiscard]] TasteCounts loadTaste();
void saveTaste(const TasteCounts& counts);

}  // namespace zyron::ui
