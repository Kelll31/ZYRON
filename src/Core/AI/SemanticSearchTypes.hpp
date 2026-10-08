// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Library/LibraryTypes.hpp"

namespace zyron::core {

/// Interpreted musical parameters extracted from natural language search queries (SPEC sections 30, 60, ROADMAP P7-04).
struct ParsedDjQuery {
  std::string originalQuery;
  std::string detectedGenre;
  double targetBpm{0.0};
  double bpmTolerance{4.0};
  std::string targetKey;
  double minEnergy{1.0};
  double maxEnergy{10.0};
  std::vector<std::string> keywords;

  bool hasGenreConstraint{false};
  bool hasBpmConstraint{false};
  bool hasKeyConstraint{false};
  bool hasEnergyConstraint{false};
};

/// A ranked result from semantic or natural language library search.
struct SemanticSearchResult {
  TrackItem track;
  float matchScore{0.0f};        // Relevance rating: 0.0 .. 1.0
  std::string matchReason;       // e.g. "Matched Neurofunk, BPM 174, High Energy (8.2)"
};

/// Abstract interface for natural language and semantic music search.
class ISemanticSearch {
 public:
  virtual ~ISemanticSearch() = default;

  /// Parses natural language query text into musical constraints and keywords.
  [[nodiscard]] virtual ParsedDjQuery parseQuery(std::string_view query) const = 0;

  /// Executes semantic search across catalog tracks, returning ranked matches.
  [[nodiscard]] virtual std::vector<SemanticSearchResult> search(
      std::string_view query,
      const std::vector<TrackItem>& catalog,
      std::size_t limit = 20) const = 0;
};

}  // namespace zyron::core
