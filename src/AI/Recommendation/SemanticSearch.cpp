// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Recommendation/SemanticSearch.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>

#include "AI/Recommendation/CompatibilityScorer.hpp"

namespace zyron::ai {

namespace {

std::string toLower(std::string_view s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return result;
}

}  // namespace

core::ParsedDjQuery SemanticSearch::parseQuery(std::string_view query) const {
  core::ParsedDjQuery parsed;
  parsed.originalQuery = std::string(query);

  const std::string lower = toLower(query);

  // 1. Detect BPM: look for e.g. "174 bpm", "174", "around 170"
  std::regex bpmRegex(R"(\b(around\s+)?(\d{2,3})(\s*bpm)?\b)");
  std::smatch bpmMatch;
  if (std::regex_search(lower, bpmMatch, bpmRegex)) {
    try {
      const int val = std::stoi(bpmMatch[2].str());
      if (val >= 60 && val <= 220) {
        parsed.targetBpm = static_cast<double>(val);
        parsed.hasBpmConstraint = true;
      }
    } catch (...) {}
  }

  // 2. Detect Camelot Key: e.g. "8A", "11B", "8a"
  std::regex keyRegex(R"(\b([1-9]|1[0-2])[ab]\b)");
  std::smatch keyMatch;
  if (std::regex_search(lower, keyMatch, keyRegex)) {
    std::string key = keyMatch[0].str();
    key.back() = static_cast<char>(std::toupper(static_cast<unsigned char>(key.back())));
    parsed.targetKey = key;
    parsed.hasKeyConstraint = true;
  }

  // 3. Detect Energy Intent
  if (lower.find("heavy") != std::string::npos || lower.find("banger") != std::string::npos ||
      lower.find("intense") != std::string::npos || lower.find("peak") != std::string::npos ||
      lower.find("hard") != std::string::npos || lower.find("drop") != std::string::npos) {
    parsed.minEnergy = 7.5;
    parsed.maxEnergy = 10.0;
    parsed.hasEnergyConstraint = true;
  } else if (lower.find("chill") != std::string::npos || lower.find("liquid") != std::string::npos ||
             lower.find("warmup") != std::string::npos || lower.find("deep") != std::string::npos ||
             lower.find("soft") != std::string::npos || lower.find("ambient") != std::string::npos) {
    parsed.minEnergy = 1.0;
    parsed.maxEnergy = 5.5;
    parsed.hasEnergyConstraint = true;
  }

  // 4. Detect Genre
  if (lower.find("neurofunk") != std::string::npos) {
    parsed.detectedGenre = "Neurofunk";
    parsed.hasGenreConstraint = true;
  } else if (lower.find("liquid") != std::string::npos) {
    parsed.detectedGenre = "Liquid";
    parsed.hasGenreConstraint = true;
  } else if (lower.find("dnb") != std::string::npos || lower.find("drum and bass") != std::string::npos ||
             lower.find("drum & bass") != std::string::npos) {
    parsed.detectedGenre = "Drum & Bass";
    parsed.hasGenreConstraint = true;
  } else if (lower.find("tech house") != std::string::npos) {
    parsed.detectedGenre = "Tech House";
    parsed.hasGenreConstraint = true;
  } else if (lower.find("house") != std::string::npos) {
    parsed.detectedGenre = "House";
    parsed.hasGenreConstraint = true;
  } else if (lower.find("techno") != std::string::npos) {
    parsed.detectedGenre = "Techno";
    parsed.hasGenreConstraint = true;
  } else if (lower.find("dubstep") != std::string::npos) {
    parsed.detectedGenre = "Dubstep";
    parsed.hasGenreConstraint = true;
  } else if (lower.find("trance") != std::string::npos) {
    parsed.detectedGenre = "Trance";
    parsed.hasGenreConstraint = true;
  }

  // 5. Keyword tokens
  std::istringstream iss(lower);
  std::string token;
  while (iss >> token) {
    if (token != "around" && token != "bpm" && token != "in" && token != "with" &&
        token != "find" && token != "track" && token != "music") {
      parsed.keywords.push_back(token);
    }
  }

  return parsed;
}

std::vector<core::SemanticSearchResult> SemanticSearch::search(
    std::string_view query,
    const std::vector<core::TrackItem>& catalog,
    std::size_t limit) const {
  std::vector<core::SemanticSearchResult> results;
  if (catalog.empty() || query.empty()) {
    return results;
  }

  const auto parsed = parseQuery(query);

  for (const auto& track : catalog) {
    float score = 0.0f;
    float weightSum = 0.0f;
    std::vector<std::string> reasons;

    // 1. BPM match
    if (parsed.hasBpmConstraint && track.bpm > 0.0) {
      weightSum += 0.35f;
      const double diff = std::abs(track.bpm - parsed.targetBpm);
      if (diff <= 3.0) {
        score += 0.35f;
        reasons.push_back("BPM " + std::to_string(static_cast<int>(std::round(track.bpm))));
      } else if (diff <= 8.0) {
        const float s = static_cast<float>(1.0 - (diff - 3.0) / 5.0 * 0.5);
        score += 0.35f * s;
        reasons.push_back("Near BPM (" + std::to_string(static_cast<int>(std::round(track.bpm))) + ")");
      }
    }

    // 2. Key match
    if (parsed.hasKeyConstraint && !track.key.empty()) {
      weightSum += 0.25f;
      const auto [keyVal, keyDesc] = CompatibilityScorer::evaluateKey(parsed.targetKey, track.key);
      score += 0.25f * keyVal;
      if (keyVal >= 0.70f) {
        reasons.push_back("Key " + track.key);
      }
    }

    // 3. Energy match
    if (parsed.hasEnergyConstraint && track.energy > 0.0) {
      weightSum += 0.25f;
      if (track.energy >= parsed.minEnergy && track.energy <= parsed.maxEnergy) {
        score += 0.25f;
        reasons.push_back("Energy " + std::to_string(static_cast<int>(std::round(track.energy))));
      } else {
        const double dist = (track.energy < parsed.minEnergy)
                                ? (parsed.minEnergy - track.energy)
                                : (track.energy - parsed.maxEnergy);
        const float s = std::clamp(static_cast<float>(1.0 - dist / 3.0), 0.0f, 0.7f);
        score += 0.25f * s;
      }
    }

    // 4. Genre match
    if (parsed.hasGenreConstraint && !track.genre.empty()) {
      weightSum += 0.25f;
      const float genreAffinity = CompatibilityScorer::evaluateGenre(parsed.detectedGenre, track.genre);
      score += 0.25f * genreAffinity;
      if (genreAffinity >= 0.8f) {
        reasons.push_back("Genre " + track.genre);
      }
    }

    // 5. Text keyword match
    if (!parsed.keywords.empty()) {
      weightSum += 0.15f;
      const std::string text = toLower(track.artist + " " + track.title + " " + track.album);
      int hits = 0;
      for (const auto& kw : parsed.keywords) {
        if (text.find(kw) != std::string::npos) {
          ++hits;
        }
      }
      if (hits > 0) {
        const float kwRatio = static_cast<float>(hits) / static_cast<float>(parsed.keywords.size());
        score += 0.15f * kwRatio;
        reasons.push_back("Keyword match");
      }
    }

    if (weightSum <= 0.0f) {
      weightSum = 1.0f;
      score = 0.5f;
    }

    const float normalizedScore = std::clamp(score / weightSum, 0.0f, 1.0f);

    if (normalizedScore >= 0.40f) {
      core::SemanticSearchResult item;
      item.track = track;
      item.matchScore = normalizedScore;

      std::ostringstream ss;
      for (std::size_t i = 0; i < reasons.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << reasons[i];
      }
      item.matchReason = ss.str();
      results.push_back(std::move(item));
    }
  }

  // Sort descending by relevance matchScore
  std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
    return a.matchScore > b.matchScore;
  });

  if (limit > 0 && results.size() > limit) {
    results.resize(limit);
  }

  return results;
}

}  // namespace zyron::ai
