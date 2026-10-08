// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Bpm/Beatgrid.hpp"

#include <charconv>
#include <iomanip>
#include <sstream>

namespace zyron::analysis {

namespace {

std::string_view extractValue(std::string_view json, std::string_view key) {
  const auto keyPos = json.find(key);
  if (keyPos == std::string_view::npos) return {};

  auto colonPos = json.find(':', keyPos + key.size());
  if (colonPos == std::string_view::npos) return {};

  auto startPos = colonPos + 1;
  while (startPos < json.size() && (json[startPos] == ' ' || json[startPos] == '\t' || json[startPos] == '\"')) {
    startPos++;
  }

  auto endPos = startPos;
  while (endPos < json.size() && json[endPos] != ',' && json[endPos] != '}' &&
         json[endPos] != ' ' && json[endPos] != '\"') {
    endPos++;
  }

  return json.substr(startPos, endPos - startPos);
}

}  // namespace

std::string BeatgridData::toJson() const {
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(3);
  ss << "{"
     << "\"bpm\":" << bpm
     << ",\"firstBeatFrame\":" << firstBeatFrame
     << ",\"sampleRate\":" << sampleRate
     << ",\"downbeatOffset\":" << downbeatOffset
     << ",\"source\":\"" << source << "\""
     << "}";
  return ss.str();
}

bool BeatgridData::fromJson(std::string_view json, BeatgridData& out) {
  if (json.empty() || json.front() != '{' || json.back() != '}') {
    return false;
  }

  const auto bpmStr = extractValue(json, "\"bpm\"");
  const auto fbfStr = extractValue(json, "\"firstBeatFrame\"");
  const auto srStr = extractValue(json, "\"sampleRate\"");
  const auto dboStr = extractValue(json, "\"downbeatOffset\"");
  const auto srcStr = extractValue(json, "\"source\"");

  if (bpmStr.empty() && fbfStr.empty()) {
    return false;
  }

  BeatgridData parsed;
  if (!bpmStr.empty()) {
    try {
      parsed.bpm = std::stod(std::string(bpmStr));
    } catch (...) {
      return false;
    }
  }

  if (!fbfStr.empty()) {
    try {
      parsed.firstBeatFrame = std::stoll(std::string(fbfStr));
    } catch (...) {
      return false;
    }
  }

  if (!srStr.empty()) {
    try {
      parsed.sampleRate = std::stoi(std::string(srStr));
    } catch (...) {
      parsed.sampleRate = 44100;
    }
  }

  if (!dboStr.empty()) {
    try {
      parsed.downbeatOffset = std::stoi(std::string(dboStr));
    } catch (...) {
      parsed.downbeatOffset = 0;
    }
  }

  if (!srcStr.empty()) {
    parsed.source = std::string(srcStr);
  }

  out = parsed;
  return true;
}

}  // namespace zyron::analysis
