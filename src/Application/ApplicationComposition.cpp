// SPDX-License-Identifier: AGPL-3.0-only
#include "Application/ApplicationComposition.hpp"

#include <chrono>
#include <cstdio>
#include <system_error>
#include <utility>

#include "Analysis/Waveform/WaveformPeaks.hpp"
#include "Application/TrackAnalyzer.hpp"

namespace zyron::application {

namespace {

std::shared_ptr<const core::WaveformData> makeWaveform(const audio::TrackBuffer& buffer) {
  if (buffer.numChannels() < 1 || buffer.numFrames() <= 0) {
    return nullptr;
  }
  const float* channels[2] = {buffer.channelData(0), buffer.channelData(buffer.numChannels() > 1 ? 1 : 0)};
  const auto peaks = analysis::WaveformGenerator::generate(channels, 2, static_cast<std::size_t>(buffer.numFrames()),
                                                           static_cast<int>(buffer.sampleRate()));
  return std::make_shared<const core::WaveformData>(peaks.toWaveformData());
}

}  // namespace

ApplicationComposition::ApplicationComposition(std::filesystem::path dataDir, std::filesystem::path recordingsDir)
    : dataDir_(std::move(dataDir)),
      recordingsDir_(recordingsDir.empty() ? dataDir_ / "recordings" : std::move(recordingsDir)),
      models_(dataDir_) {
  std::error_code ec;
  std::filesystem::create_directories(dataDir_, ec);

  try {
    library_ = std::make_shared<library::LibraryService>(dataDir_ / "library.db", dataDir_ / "library_folders.txt");
  } catch (const std::exception& error) {
    libraryError_ = error.what();
  }

  if (library_ != nullptr) {
    TrackAnalyzer::attach(*library_, dataDir_ / "cache", &models_);  // tempo, grid, key and energy, in the background
  }

  audio::AudioEngine::Services services;
  if (library_ != nullptr) {
    services.resolvePath = [library = library_](core::TrackId id) -> std::optional<std::filesystem::path> {
      const auto track = library->findTrack(id.value);
      if (!track.has_value() || track->filepath.empty()) {
        return std::nullopt;
      }
      const std::u8string utf8Path(reinterpret_cast<const char8_t*>(track->filepath.data()), track->filepath.size());
      return std::filesystem::path(utf8Path);
    };
  }
  if (library_ != nullptr) {
    services.gridFor = [library = library_](core::TrackId id) -> std::optional<audio::AudioEngine::GridInfo> {
      const auto track = library->findTrack(id.value);
      if (!track.has_value() || track->bpm <= 0.0) {
        return std::nullopt;
      }
      return audio::AudioEngine::GridInfo{track->bpm, track->firstBeatSec};
    };
  }
  services.makeWaveform = makeWaveform;
  services.separateStems = [this](core::DeckId deck, core::TrackId track) {
    if (stems_ != nullptr) {
      stems_->request(deck, track);
    }
  };
  services.trackReady = [this](core::DeckId deck, core::TrackId track) {
    if (stems_ != nullptr) {
      stems_->trackReady(deck, track);
    }
  };
  services.setRecording = [this](bool enabled) { return setRecording(enabled); };

  engine_ = std::make_shared<audio::AudioEngine>(std::move(services));
  bus_.addSink(engine_);
  stems_ = std::make_unique<StemService>(*engine_, library_.get(), models_, dataDir_ / "cache" / "stems");
  if (library_ != nullptr) {
    automix_ = std::make_unique<AutomixController>(bus_, *engine_, *library_);
  }
}

std::string ApplicationComposition::setRecording(bool enabled) {
  if (!enabled) {
    engine_->setMasterTap(nullptr);
    recorder_.stop();
    return {};
  }
  if (recorder_.isRecording()) {
    return {};
  }
  std::error_code ec;
  std::filesystem::create_directories(recordingsDir_, ec);
  // UTC time stamp from the C++20 calendar types: no platform-specific localtime variants.
  using namespace std::chrono;
  const auto now = floor<seconds>(system_clock::now());
  const year_month_day date{floor<days>(now)};
  const hh_mm_ss<seconds> time{now - floor<days>(now)};
  char name[48];
  std::snprintf(name, sizeof(name), "ZYRON-%04d%02u%02u-%02d%02d%02d.wav", static_cast<int>(date.year()),
                static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                static_cast<int>(time.hours().count()), static_cast<int>(time.minutes().count()),
                static_cast<int>(time.seconds().count()));

  const double sampleRate = engine_->stats().sampleRate > 0.0 ? engine_->stats().sampleRate : 48000.0;
  std::string error;
  if (!recorder_.start(recordingsDir_ / name, sampleRate, 2, true, &error)) {
    return "Recording failed: " + error;
  }
  engine_->setMasterTap(&recorder_);
  return {};
}

ApplicationComposition::~ApplicationComposition() {
  automix_.reset();
  stems_.reset();
  engine_->setMasterTap(nullptr);
  recorder_.stop();
  bus_.removeSink(engine_);
  engine_.reset();
  library_.reset();
}

}  // namespace zyron::application
