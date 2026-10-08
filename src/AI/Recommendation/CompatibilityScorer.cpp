// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Recommendation/CompatibilityScorer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

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

bool parseCamelot(std::string_view keyStr, int& outNum, char& outLetter) {
  if (keyStr.empty()) return false;

  std::string s(keyStr);
  // Strip spaces
  s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
  if (s.empty()) return false;

  const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(s.back())));
  if (last != 'A' && last != 'B') return false;

  outLetter = last;
  s.pop_back();

  try {
    const int num = std::stoi(s);
    if (num < 1 || num > 12) return false;
    outNum = num;
    return true;
  } catch (...) {
    return false;
  }
}

int circularDistance(int a, int b, int mod = 12) {
  const int diff = std::abs(a - b);
  return std::min(diff, mod - diff);
}

}  // namespace

std::pair<float, float> CompatibilityScorer::evaluateBpm(double sourceBpm, double candidateBpm) noexcept {
  if (sourceBpm <= 10.0 || candidateBpm <= 10.0) {
    return {0.5f, 0.0f};
  }

  // Resolve octave relations (half-time / double-time matching)
  if (sourceBpm > 140.0 && candidateBpm < 100.0 && std::abs(candidateBpm * 2.0 - sourceBpm) < 20.0) {
    candidateBpm *= 2.0;
  } else if (sourceBpm < 100.0 && candidateBpm > 140.0 && std::abs(candidateBpm / 2.0 - sourceBpm) < 20.0) {
    candidateBpm /= 2.0;
  }

  const double pitchShift = (candidateBpm - sourceBpm) / sourceBpm * 100.0;
  const double absShift = std::abs(pitchShift);

  float score = 0.0f;
  if (absShift <= 2.0) {
    score = 1.0f;
  } else if (absShift <= 6.0) {
    score = static_cast<float>(1.0 - (absShift - 2.0) / 4.0 * 0.2);  // 0.8 .. 1.0
  } else if (absShift <= 10.0) {
    score = static_cast<float>(0.8 - (absShift - 6.0) / 4.0 * 0.4);  // 0.4 .. 0.8
  } else if (absShift <= 16.0) {
    score = static_cast<float>(0.4 - (absShift - 10.0) / 6.0 * 0.35); // 0.05 .. 0.4
  } else {
    score = 0.0f;
  }

  return {score, static_cast<float>(pitchShift)};
}

std::pair<float, std::string> CompatibilityScorer::evaluateKey(
    const std::string& sourceKey, const std::string& candidateKey) noexcept {
  int srcNum = 0, candNum = 0;
  char srcLet = 'A', candLet = 'A';

  if (!parseCamelot(sourceKey, srcNum, srcLet) || !parseCamelot(candidateKey, candNum, candLet)) {
    return {0.5f, "Key undetermined"};
  }

  // 1. Exact match (e.g. 8A -> 8A)
  if (srcNum == candNum && srcLet == candLet) {
    return {1.0f, "Identical key"};
  }

  // 2. Relative Major/Minor (e.g. 8A -> 8B)
  if (srcNum == candNum && srcLet != candLet) {
    return {0.92f, "Relative key"};
  }

  const int dist = circularDistance(srcNum, candNum, 12);

  // 3. Adjacent key (+1 / -1 hour, same mode)
  if (dist == 1 && srcLet == candLet) {
    return {0.85f, "Adjacent key (+1/-1)"};
  }

  // 4. Dominant relative (+1 / -1 hour, different mode)
  if (dist == 1 && srcLet != candLet) {
    return {0.72f, "Dominant relative (+1/-1 letter)"};
  }

  // 5. Energy boost / Modulation (+2 hours, e.g. 8A -> 10A)
  if (dist == 2 && srcLet == candLet) {
    return {0.65f, "Energy boost (+2 semitones)"};
  }

  // 6. Dissonant / distant key
  return {0.20f, "Key clash / Distant"};
}

float CompatibilityScorer::evaluateEnergy(
    double sourceEnergy, double candidateEnergy, core::EnergyGoal goal) noexcept {
  const double delta = candidateEnergy - sourceEnergy;

  switch (goal) {
    case core::EnergyGoal::Maintain: {
      const double absDelta = std::abs(delta);
      return std::clamp(static_cast<float>(1.0 - absDelta / 2.5), 0.0f, 1.0f);
    }
    case core::EnergyGoal::BuildUp: {
      if (delta >= 0.8 && delta <= 2.5) {
        return 1.0f;
      }
      if (delta > 2.5) {
        return std::clamp(static_cast<float>(1.0 - (delta - 2.5) / 3.0), 0.3f, 1.0f);
      }
      // Flat or falling energy penalized
      return std::clamp(static_cast<float>(0.5 + delta / 2.0), 0.0f, 0.7f);
    }
    case core::EnergyGoal::CoolDown: {
      if (delta <= -0.8 && delta >= -2.5) {
        return 1.0f;
      }
      if (delta < -2.5) {
        return std::clamp(static_cast<float>(1.0 - (-delta - 2.5) / 3.0), 0.3f, 1.0f);
      }
      // Flat or rising energy penalized
      return std::clamp(static_cast<float>(0.5 - delta / 2.0), 0.0f, 0.7f);
    }
    case core::EnergyGoal::Any:
      return 0.85f;
  }
  return 0.5f;
}

float CompatibilityScorer::evaluateGenre(
    const std::string& sourceGenre, const std::string& candidateGenre) noexcept {
  if (sourceGenre.empty() || candidateGenre.empty()) {
    return 0.75f;
  }

  const std::string a = toLower(sourceGenre);
  const std::string b = toLower(candidateGenre);

  if (a == b) return 1.0f;

  if (a.find(b) != std::string::npos || b.find(a) != std::string::npos) {
    return 0.88f;
  }

  // DnB Family
  const bool isDnbA = (a.find("dnb") != std::string::npos || a.find("drum") != std::string::npos ||
                       a.find("liquid") != std::string::npos || a.find("neurofunk") != std::string::npos ||
                       a.find("jungle") != std::string::npos);
  const bool isDnbB = (b.find("dnb") != std::string::npos || b.find("drum") != std::string::npos ||
                       b.find("liquid") != std::string::npos || b.find("neurofunk") != std::string::npos ||
                       b.find("jungle") != std::string::npos);
  if (isDnbA && isDnbB) return 0.85f;

  // House / Techno Family
  const bool isHouseA = (a.find("house") != std::string::npos || a.find("techno") != std::string::npos ||
                         a.find("electro") != std::string::npos);
  const bool isHouseB = (b.find("house") != std::string::npos || b.find("techno") != std::string::npos ||
                         b.find("electro") != std::string::npos);
  if (isHouseA && isHouseB) return 0.85f;

  return 0.30f;
}

core::CompatibilityScore CompatibilityScorer::score(
    const core::TrackItem& source,
    const core::TrackItem& candidate,
    core::EnergyGoal goal) {
  core::CompatibilityScore result;

  // 1. BPM
  const auto [bpmScore, pitchShift] = evaluateBpm(source.bpm, candidate.bpm);
  result.bpmScore = bpmScore;
  result.pitchBendPercent = pitchShift;
  result.bpmDelta = static_cast<float>(candidate.bpm - source.bpm);
  result.isBpmCompatible = (std::abs(pitchShift) <= 8.0f);

  // 2. Key
  const auto [keyScore, keyRel] = evaluateKey(source.key, candidate.key);
  result.keyScore = keyScore;
  result.keyRelation = keyRel;
  result.isHarmonic = (keyScore >= 0.70f);

  // 3. Energy
  result.energyScore = evaluateEnergy(source.energy, candidate.energy, goal);

  // 4. Genre
  result.genreScore = evaluateGenre(source.genre, candidate.genre);

  // 5. Structure match factor
  result.structureScore = 0.85f;
  if (source.durationSec > 0.0 && candidate.durationSec > 0.0) {
    const double ratio = std::min(source.durationSec, candidate.durationSec) /
                         std::max(source.durationSec, candidate.durationSec);
    result.structureScore = std::clamp(static_cast<float>(0.7 + 0.3 * ratio), 0.7f, 1.0f);
  }

  // Composite overall compatibility
  result.overallScore = std::clamp(
      0.35f * result.bpmScore +
      0.30f * result.keyScore +
      0.20f * result.energyScore +
      0.15f * result.genreScore,
      0.0f, 1.0f);

  // Build explanation
  std::ostringstream ss;
  if (std::abs(result.pitchBendPercent) < 0.2f) {
    ss << "Exact BPM (" << std::fixed << std::setprecision(1) << candidate.bpm << "). ";
  } else {
    ss << "BPM " << std::fixed << std::setprecision(1) << candidate.bpm
       << " (" << (result.pitchBendPercent > 0 ? "+" : "") << result.pitchBendPercent << "%). ";
  }

  ss << result.keyRelation << " (" << (candidate.key.empty() ? "?" : candidate.key) << "). ";

  const double energyDelta = candidate.energy - source.energy;
  if (std::abs(energyDelta) < 0.5) {
    ss << "Matched energy (" << std::fixed << std::setprecision(1) << candidate.energy << ").";
  } else if (energyDelta > 0) {
    ss << "Higher energy (+" << std::fixed << std::setprecision(1) << energyDelta << ").";
  } else {
    ss << "Lower energy (" << std::fixed << std::setprecision(1) << energyDelta << ").";
  }

  result.explanation = ss.str();
  return result;
}

}  // namespace zyron::ai
