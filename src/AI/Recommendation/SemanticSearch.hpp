// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string_view>
#include <vector>

#include "Core/AI/SemanticSearchTypes.hpp"

namespace zyron::ai {

/// Natural-language and parameter-aware DJ music search (SPEC sections 30, 60, ROADMAP P7-04).
class SemanticSearch final : public core::ISemanticSearch {
 public:
  SemanticSearch() = default;
  ~SemanticSearch() override = default;

  [[nodiscard]] core::ParsedDjQuery parseQuery(std::string_view query) const override;

  [[nodiscard]] std::vector<core::SemanticSearchResult> search(
      std::string_view query,
      const std::vector<core::TrackItem>& catalog,
      std::size_t limit = 20) const override;
};

}  // namespace zyron::ai
