// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Bpm/BeatgridEditor.hpp"

#include <algorithm>
#include <cmath>

namespace zyron::analysis {

BeatgridEditor::BeatgridEditor(BeatgridData data)
    : data_(std::move(data)) {}

void BeatgridEditor::setBpm(double newBpm) {
  if (newBpm <= 0.0) return;
  // Sane bounds for DJ music
  data_.bpm = std::clamp(newBpm, 20.0, 400.0);
  data_.source = "user";
}

void BeatgridEditor::adjustBpm(double deltaBpm) {
  setBpm(data_.bpm + deltaBpm);
}

void BeatgridEditor::setFirstBeat(std::int64_t frame) {
  data_.firstBeatFrame = frame;
  data_.source = "user";
}

void BeatgridEditor::shiftPhase(std::int64_t deltaFrames) {
  data_.firstBeatFrame += deltaFrames;
  data_.source = "user";
}

void BeatgridEditor::shiftPhaseMs(double deltaMs) {
  if (data_.sampleRate <= 0) return;
  const auto deltaFrames = static_cast<std::int64_t>(
      std::round(deltaMs * 0.001 * static_cast<double>(data_.sampleRate)));
  shiftPhase(deltaFrames);
}

void BeatgridEditor::setDownbeatOffset(int offset) {
  data_.downbeatOffset = (offset % 4 + 4) % 4;
  data_.source = "user";
}

void BeatgridEditor::tapTempo(std::int64_t tapFrame) {
  if (data_.sampleRate <= 0) return;

  if (!tapHistory_.empty()) {
    const auto diff = tapFrame - tapHistory_.back();
    // Reset if taps are reversed, or if interval exceeds 2.5 seconds (slower than 24 BPM)
    const auto timeoutFrames = static_cast<std::int64_t>(data_.sampleRate * 2.5);
    if (diff <= 0 || diff > timeoutFrames) {
      tapHistory_.clear();
    }
  }

  tapHistory_.push_back(tapFrame);
  if (tapHistory_.size() > 8) {
    tapHistory_.erase(tapHistory_.begin());
  }

  if (tapHistory_.size() >= 4) {
    double sumDiff = 0.0;
    for (std::size_t i = 1; i < tapHistory_.size(); ++i) {
      sumDiff += static_cast<double>(tapHistory_[i] - tapHistory_[i - 1]);
    }
    const double avgInterval = sumDiff / static_cast<double>(tapHistory_.size() - 1);
    if (avgInterval > 0.0) {
      const double tappedBpm = (static_cast<double>(data_.sampleRate) * 60.0) / avgInterval;
      setBpm(tappedBpm);
    }
  }
}

void BeatgridEditor::resetTapTempo() noexcept {
  tapHistory_.clear();
}

void BeatgridEditor::setSampleRate(int newSampleRate) {
  if (newSampleRate <= 0 || newSampleRate == data_.sampleRate) return;
  const double ratio = static_cast<double>(newSampleRate) / static_cast<double>(data_.sampleRate);
  data_.firstBeatFrame = static_cast<std::int64_t>(std::round(static_cast<double>(data_.firstBeatFrame) * ratio));
  data_.sampleRate = newSampleRate;
}

void BeatgridEditor::setSource(std::string_view source) {
  data_.source = std::string(source);
}

std::int64_t BeatgridEditor::findNearestBeat(std::int64_t currentFrame) const {
  const double spb = data_.samplesPerBeat();
  if (spb <= 0.0) return currentFrame;
  const double beatIdx = static_cast<double>(currentFrame - data_.firstBeatFrame) / spb;
  const auto nearest = static_cast<std::int64_t>(std::round(beatIdx));
  return data_.firstBeatFrame + static_cast<std::int64_t>(std::round(static_cast<double>(nearest) * spb));
}

std::int64_t BeatgridEditor::findNextBeat(std::int64_t currentFrame) const {
  const double spb = data_.samplesPerBeat();
  if (spb <= 0.0) return currentFrame;
  const double beatIdx = static_cast<double>(currentFrame - data_.firstBeatFrame) / spb;
  const auto nextIdx = static_cast<std::int64_t>(std::floor(beatIdx)) + 1;
  return data_.firstBeatFrame + static_cast<std::int64_t>(std::round(static_cast<double>(nextIdx) * spb));
}

std::int64_t BeatgridEditor::findPreviousBeat(std::int64_t currentFrame) const {
  const double spb = data_.samplesPerBeat();
  if (spb <= 0.0) return currentFrame;
  const double beatIdx = static_cast<double>(currentFrame - data_.firstBeatFrame) / spb;
  const auto prevIdx = static_cast<std::int64_t>(std::floor(beatIdx));
  return data_.firstBeatFrame + static_cast<std::int64_t>(std::round(static_cast<double>(prevIdx) * spb));
}

double BeatgridEditor::getBeatFraction(std::int64_t currentFrame) const {
  const double spb = data_.samplesPerBeat();
  if (spb <= 0.0) return 0.0;
  const double beatIdx = static_cast<double>(currentFrame - data_.firstBeatFrame) / spb;
  double frac = beatIdx - std::floor(beatIdx);
  if (frac < 0.0) frac += 1.0;
  return frac;
}

BarBeatPosition BeatgridEditor::getBarBeat(std::int64_t currentFrame) const {
  const double spb = data_.samplesPerBeat();
  if (spb <= 0.0) return BarBeatPosition{};

  const double beatIdxDouble = static_cast<double>(currentFrame - data_.firstBeatFrame) / spb;
  const auto beatIdx = static_cast<std::int64_t>(std::floor(beatIdxDouble));
  double fraction = beatIdxDouble - static_cast<double>(beatIdx);
  if (fraction < 0.0) fraction += 1.0;

  const std::int64_t adjustedBeat = beatIdx - data_.downbeatOffset;
  std::int64_t barIndex = adjustedBeat / 4;
  int beatInBar = static_cast<int>(adjustedBeat % 4) + 1;
  if (beatInBar <= 0) {
    beatInBar += 4;
    barIndex -= 1;
  }

  return BarBeatPosition{barIndex, beatInBar, fraction};
}

std::vector<std::int64_t> BeatgridEditor::generateBeatFrames(std::size_t totalAudioFrames) const {
  std::vector<std::int64_t> beats;
  const double spb = data_.samplesPerBeat();
  if (spb <= 0.0 || totalAudioFrames == 0) return beats;

  std::int64_t minIdx = 0;
  if (data_.firstBeatFrame < 0) {
    minIdx = static_cast<std::int64_t>(std::ceil(-static_cast<double>(data_.firstBeatFrame) / spb));
  }

  for (std::int64_t b = minIdx; ; ++b) {
    const auto f = data_.firstBeatFrame + static_cast<std::int64_t>(std::round(static_cast<double>(b) * spb));
    if (f >= static_cast<std::int64_t>(totalAudioFrames)) break;
    beats.push_back(f);
  }

  return beats;
}

std::vector<std::int64_t> BeatgridEditor::generateDownbeatFrames(std::size_t totalAudioFrames) const {
  std::vector<std::int64_t> downbeats;
  const double spb = data_.samplesPerBeat();
  if (spb <= 0.0 || totalAudioFrames == 0) return downbeats;

  std::int64_t minIdx = 0;
  if (data_.firstBeatFrame < 0) {
    minIdx = static_cast<std::int64_t>(std::ceil(-static_cast<double>(data_.firstBeatFrame) / spb));
  }

  for (std::int64_t b = minIdx; ; ++b) {
    const auto f = data_.firstBeatFrame + static_cast<std::int64_t>(std::round(static_cast<double>(b) * spb));
    if (f >= static_cast<std::int64_t>(totalAudioFrames)) break;
    if (((b - data_.downbeatOffset) % 4 + 4) % 4 == 0) {
      downbeats.push_back(f);
    }
  }

  return downbeats;
}

}  // namespace zyron::analysis
