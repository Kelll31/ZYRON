// SPDX-License-Identifier: AGPL-3.0-only
#include "Application/AnalysisSidecar.hpp"

#include <juce_core/juce_core.h>

namespace zyron::application {

namespace {

constexpr int kFormatVersion = 1;

juce::File toJuceFile(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return juce::File(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()), static_cast<int>(utf8.size())));
}

std::string toUtf8(const juce::String& text) {
  return text.toStdString();  // juce::String::toStdString is UTF-8
}

juce::String relativePath(const std::filesystem::path& track, const std::filesystem::path& folder) {
  std::filesystem::path rel = track.lexically_relative(folder);
  if (rel.empty() || rel.native().starts_with(std::filesystem::path("..").native())) {
    rel = track.filename();
  }
  const std::u8string utf8 = rel.generic_u8string();
  return juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()), static_cast<int>(utf8.size()));
}

}  // namespace

struct AnalysisSidecar::Folder {
  juce::File file;
  juce::DynamicObject::Ptr tracks{new juce::DynamicObject()};
};

AnalysisSidecar::AnalysisSidecar(FolderResolver resolver) : resolver_(std::move(resolver)) {}

AnalysisSidecar::~AnalysisSidecar() = default;

AnalysisSidecar::Folder& AnalysisSidecar::folderFor(const std::filesystem::path& folder) {
  auto& slot = folders_[folder];
  if (slot == nullptr) {
    slot = std::make_unique<Folder>();
    slot->file = toJuceFile(folder / kFileName);
    if (slot->file.existsAsFile()) {
      const juce::var root = juce::JSON::parse(slot->file);
      if (auto* object = root.getProperty("tracks", {}).getDynamicObject()) {
        slot->tracks = object;  // a file from a newer or broken format simply yields no entries
      }
    }
  }
  return *slot;
}

bool AnalysisSidecar::load(const std::filesystem::path& track, const std::string& contentHash, int analysisVersion,
                           int structureVersion, TrackAnalysis& out, int* storedStructureVersion) {
  if (contentHash.empty() || !resolver_) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  Folder& folder = folderFor(resolver_(track));
  const juce::var entry = folder.tracks->getProperty(juce::Identifier(juce::String(contentHash)));
  if (!entry.isObject() || static_cast<int>(entry["analysisVersion"]) != analysisVersion ||
      (structureVersion >= 0 && static_cast<int>(entry["structureVersion"]) != structureVersion) ||
      static_cast<int>(entry["sampleRate"]) <= 0) {
    return false;
  }

  TrackAnalysis result;
  result.sampleRate = static_cast<int>(entry["sampleRate"]);
  result.bpm = static_cast<double>(entry["bpm"]);
  result.beatOk = result.bpm > 0.0;
  result.firstBeatFrame = static_cast<std::int64_t>(static_cast<juce::int64>(entry["firstBeatFrame"]));
  result.gridJson = toUtf8(entry["gridJson"].toString());
  result.beatMethod = toUtf8(entry["beatMethod"].toString());
  result.camelot = toUtf8(entry["key"].toString());
  result.keyOk = !result.camelot.empty();
  result.keyMethod = toUtf8(entry["keyMethod"].toString());
  result.energy = static_cast<double>(entry["energy"]);
  result.energyOk = entry.hasProperty("energy");
  if (entry.hasProperty("loudnessLufs")) {
    result.loudnessLufs = static_cast<double>(entry["loudnessLufs"]);
    result.loudnessOk = result.loudnessLufs < 0.0;
  }
  result.vocalMethod = toUtf8(entry["vocalMethod"].toString());
  result.barProfileOk = analysis::BarProfile::decode(toUtf8(entry["barProfile"].toString()), result.barProfile);
  if (const auto* markers = entry["markers"].getArray()) {
    for (const juce::var& marker : *markers) {
      result.markers.push_back({static_cast<double>(marker["time"]), toUtf8(marker["type"].toString()),
                                toUtf8(marker["name"].toString())});
    }
  }
  result.structureOk = !result.markers.empty();
  if (storedStructureVersion != nullptr) {
    *storedStructureVersion = static_cast<int>(entry["structureVersion"]);
  }
  out = std::move(result);
  return true;
}

void AnalysisSidecar::store(const std::filesystem::path& track, const std::string& contentHash, int analysisVersion,
                            int structureVersion, const TrackAnalysis& analysis) {
  if (contentHash.empty() || !resolver_) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const std::filesystem::path folderPath = resolver_(track);
  Folder& folder = folderFor(folderPath);

  auto* entry = new juce::DynamicObject();
  entry->setProperty("path", relativePath(track, folderPath));
  entry->setProperty("analysisVersion", analysisVersion);
  entry->setProperty("structureVersion", structureVersion);
  entry->setProperty("sampleRate", analysis.sampleRate);
  if (analysis.beatOk) {
    entry->setProperty("bpm", analysis.bpm);
    entry->setProperty("firstBeatFrame", static_cast<juce::int64>(analysis.firstBeatFrame));
    entry->setProperty("gridJson", juce::String(analysis.gridJson));
    entry->setProperty("beatMethod", juce::String(analysis.beatMethod));
  }
  if (analysis.keyOk) {
    entry->setProperty("key", juce::String(analysis.camelot));
    entry->setProperty("keyMethod", juce::String(analysis.keyMethod));
  }
  if (analysis.energyOk) {
    entry->setProperty("energy", analysis.energy);
  }
  if (analysis.loudnessOk) {
    entry->setProperty("loudnessLufs", analysis.loudnessLufs);
  }
  if (analysis.barProfileOk) {
    entry->setProperty("barProfile", juce::String(analysis.barProfile.encode()));
  }
  if (!analysis.vocalMethod.empty()) {
    entry->setProperty("vocalMethod", juce::String(analysis.vocalMethod));
  }
  juce::Array<juce::var> markers;
  for (const auto& marker : analysis.markers) {
    auto* m = new juce::DynamicObject();
    m->setProperty("time", marker.timeSec);
    m->setProperty("type", juce::String(marker.type));
    m->setProperty("name", juce::String::fromUTF8(marker.name.c_str()));
    markers.add(juce::var(m));
  }
  entry->setProperty("markers", markers);
  folder.tracks->setProperty(juce::Identifier(juce::String(contentHash)), juce::var(entry));

  auto* root = new juce::DynamicObject();
  root->setProperty("format", "zyron-analysis");
  root->setProperty("version", kFormatVersion);
  root->setProperty("note", "Track analysis by ZYRON (tempo, beat grid, key, energy, loudness, bar energy, vocals, drops, mix points). Safe to delete: "
                            "ZYRON analyses the tracks again.");
  root->setProperty("tracks", juce::var(folder.tracks.get()));
  // replaceWithText writes a temporary file and moves it over the old one.
  (void)folder.file.replaceWithText(juce::JSON::toString(juce::var(root)), false, false, "\n");
}

}  // namespace zyron::application
