// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Structure/StructureSegmenter.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace zyron::analysis {

namespace {

std::int64_t secToFrame(double sec, int sampleRate) noexcept {
  return std::max<std::int64_t>(0, static_cast<std::int64_t>(std::round(sec * static_cast<double>(sampleRate))));
}

double frameToSec(std::int64_t frame, int sampleRate) noexcept {
  return (sampleRate > 0) ? (static_cast<double>(frame) / static_cast<double>(sampleRate)) : 0.0;
}

std::int64_t snapToNearestBar(std::int64_t targetFrame, const BeatgridData& grid) noexcept {
  const double samplesPerBar = 4.0 * grid.samplesPerBeat();
  if (samplesPerBar <= 1.0) return targetFrame;

  const double offset = static_cast<double>(targetFrame - grid.firstBeatFrame);
  const double barIndex = std::round(offset / samplesPerBar);
  const std::int64_t snapped = grid.firstBeatFrame + static_cast<std::int64_t>(barIndex * samplesPerBar);
  return std::max<std::int64_t>(0, snapped);
}

int calculateBarNumber(std::int64_t frame, const BeatgridData& grid) noexcept {
  const double samplesPerBar = 4.0 * grid.samplesPerBeat();
  if (samplesPerBar <= 1.0) return 1;

  const double offset = static_cast<double>(frame - grid.firstBeatFrame);
  const int bar = static_cast<int>(std::floor(offset / samplesPerBar)) + 1;
  return std::max(1, bar);
}

void computeSegmentEnergies(TrackSegment& seg, const std::vector<float>& energyCurve, float stepSec) {
  if (energyCurve.empty() || stepSec <= 0.0f) {
    seg.averageEnergy = 5.0f;
    seg.peakEnergy = 5.0f;
    return;
  }

  const std::size_t startIdx = std::min(energyCurve.size() - 1,
      static_cast<std::size_t>(std::max(0.0, seg.startSec) / static_cast<double>(stepSec)));
  const std::size_t endIdx = std::min(energyCurve.size() - 1,
      static_cast<std::size_t>(std::max(0.0, seg.endSec) / static_cast<double>(stepSec)));

  if (startIdx > endIdx) {
    seg.averageEnergy = energyCurve[startIdx];
    seg.peakEnergy = energyCurve[startIdx];
    return;
  }

  double sum = 0.0;
  float peak = 0.0f;
  std::size_t count = 0;

  for (std::size_t i = startIdx; i <= endIdx; ++i) {
    sum += energyCurve[i];
    peak = std::max(peak, energyCurve[i]);
    ++count;
  }

  seg.averageEnergy = (count > 0) ? static_cast<float>(sum / static_cast<double>(count)) : 5.0f;
  seg.peakEnergy = peak;
}

}  // namespace

TrackStructure StructureSegmenter::analyze(
    const float* audio,
    std::size_t numSamples,
    int sampleRate,
    const BeatgridData* beatgrid,
    const EnergyResult* energy) const {
  TrackStructure result;
  if (!audio || numSamples == 0 || sampleRate <= 0) {
    return result;
  }

  const double totalDuration = frameToSec(static_cast<std::int64_t>(numSamples), sampleRate);
  result.durationSec = totalDuration;
  result.totalFrames = static_cast<std::int64_t>(numSamples);

  // Very short audio handling (< 10s)
  if (totalDuration < 10.0) {
    TrackSegment seg;
    seg.type = SegmentType::Intro;
    seg.startFrame = 0;
    seg.endFrame = result.totalFrames;
    seg.startSec = 0.0;
    seg.endSec = totalDuration;
    seg.startBar = 1;
    seg.endBar = 1;
    seg.averageEnergy = 5.0f;
    seg.peakEnergy = 5.0f;
    result.segments.push_back(seg);
    return result;
  }

  // Precomputed or internal energy analysis
  EnergyResult computedEnergy;
  const EnergyResult& e = energy ? *energy : (computedEnergy = EnergyAnalyzer().analyze(audio, numSamples, sampleRate));

  const auto& curve = e.energyCurve;
  const float stepSec = (e.curveStepSec > 0.01f) ? e.curveStepSec : 0.5f;

  if (curve.empty()) {
    TrackSegment seg;
    seg.type = SegmentType::Intro;
    seg.startFrame = 0;
    seg.endFrame = result.totalFrames;
    seg.startSec = 0.0;
    seg.endSec = totalDuration;
    result.segments.push_back(seg);
    return result;
  }

  // Calculate energy statistics
  std::vector<float> sortedCurve = curve;
  std::sort(sortedCurve.begin(), sortedCurve.end());
  const float medianEnergy = sortedCurve[sortedCurve.size() / 2];
  const float p75Energy = sortedCurve[static_cast<std::size_t>(0.75f * static_cast<float>(sortedCurve.size() - 1))];
  const float maxEnergy = sortedCurve.back();

  // Find landmarks in time (seconds)
  // 1. First Drop (Drop 1): First significant climb to >= p75Energy in first half of track
  const std::size_t minDropIdx = static_cast<std::size_t>(std::max(0.08 * totalDuration, 5.0) / stepSec);
  const std::size_t maxDropIdx = static_cast<std::size_t>(std::min(0.55 * totalDuration, totalDuration - 10.0) / stepSec);

  std::size_t drop1Idx = 0;
  for (std::size_t i = minDropIdx; i <= maxDropIdx && i < curve.size(); ++i) {
    if (curve[i] >= p75Energy && curve[i] >= medianEnergy + 1.0f) {
      // Check if sustained for at least 3 seconds
      bool sustained = true;
      const std::size_t lookahead = std::min(curve.size(), i + static_cast<std::size_t>(3.0 / stepSec));
      for (std::size_t j = i; j < lookahead; ++j) {
        if (curve[j] < medianEnergy) {
          sustained = false;
          break;
        }
      }
      if (sustained) {
        drop1Idx = i;
        break;
      }
    }
  }

  // If no peak met p75, default to ~25% into track
  if (drop1Idx == 0) {
    drop1Idx = std::min(curve.size() - 1, static_cast<std::size_t>(0.25 * totalDuration / stepSec));
  }

  double drop1Sec = static_cast<double>(drop1Idx) * static_cast<double>(stepSec);

  // 2. Build 1: 8-15 seconds leading into drop 1
  const double buildDuration = std::min(15.0, drop1Sec * 0.4);
  const double build1Sec = std::max(0.0, drop1Sec - buildDuration);

  // 3. Breakdown (Break): Energy drop following Drop 1
  const std::size_t drop1MinEnd = drop1Idx + static_cast<std::size_t>(std::max(12.0, 0.12 * totalDuration) / stepSec);
  std::size_t breakIdx = 0;
  for (std::size_t i = drop1MinEnd; i < curve.size() && static_cast<double>(i) * stepSec < 0.70 * totalDuration; ++i) {
    if (curve[i] <= medianEnergy + 0.5f && (maxEnergy - curve[i]) >= 2.0f) {
      breakIdx = i;
      break;
    }
  }

  double breakSec = (breakIdx > 0)
                        ? static_cast<double>(breakIdx) * static_cast<double>(stepSec)
                        : std::min(totalDuration * 0.55, drop1Sec + 35.0);

  // 4. Second Drop (Drop 2): Energy climax following breakdown
  std::size_t drop2Idx = 0;
  if (breakSec + 15.0 < totalDuration) {
    const std::size_t breakEndIdx = static_cast<std::size_t>((breakSec + 10.0) / stepSec);
    const std::size_t drop2MaxIdx = static_cast<std::size_t>(0.85 * totalDuration / stepSec);

    for (std::size_t i = breakEndIdx; i <= drop2MaxIdx && i < curve.size(); ++i) {
      if (curve[i] >= p75Energy && curve[i] >= medianEnergy + 1.0f) {
        drop2Idx = i;
        break;
      }
    }
  }

  double drop2Sec = (drop2Idx > 0) ? static_cast<double>(drop2Idx) * static_cast<double>(stepSec) : 0.0;

  // 5. Outro: Final section where energy descends
  const double outroDuration = std::min(30.0, std::max(10.0, totalDuration * 0.15));
  double outroSec = std::max(totalDuration - outroDuration,
                             (drop2Sec > 0.0) ? drop2Sec + 25.0 : drop1Sec + 40.0);
  if (outroSec >= totalDuration - 5.0) {
    outroSec = totalDuration - 10.0;
  }

  // Build raw frame boundary points
  struct Boundary {
    std::int64_t frame{0};
    SegmentType nextType{SegmentType::Intro};
  };

  std::vector<Boundary> boundaries;
  boundaries.push_back({0, SegmentType::Intro});

  if (build1Sec > 3.0 && build1Sec < drop1Sec) {
    boundaries.push_back({secToFrame(build1Sec, sampleRate), SegmentType::Build});
  }

  boundaries.push_back({secToFrame(drop1Sec, sampleRate), SegmentType::Drop});

  if (breakSec > drop1Sec + 10.0 && breakSec < outroSec) {
    boundaries.push_back({secToFrame(breakSec, sampleRate), SegmentType::Break});
  }

  if (drop2Sec > breakSec + 10.0 && drop2Sec < outroSec) {
    boundaries.push_back({secToFrame(drop2Sec, sampleRate), SegmentType::Drop2});
  }

  if (outroSec > boundaries.back().frame / sampleRate + 5.0 && outroSec < totalDuration) {
    boundaries.push_back({secToFrame(outroSec, sampleRate), SegmentType::Outro});
  }

  // Snap to beatgrid if available
  if (beatgrid && beatgrid->bpm > 20.0) {
    for (std::size_t i = 1; i < boundaries.size(); ++i) {
      boundaries[i].frame = snapToNearestBar(boundaries[i].frame, *beatgrid);
    }
  }

  // Ensure strict monotonicity of boundaries
  for (std::size_t i = 1; i < boundaries.size(); ++i) {
    if (boundaries[i].frame <= boundaries[i - 1].frame) {
      boundaries[i].frame = boundaries[i - 1].frame + sampleRate;  // At least 1 second apart
    }
  }

  // Construct segments
  for (std::size_t i = 0; i < boundaries.size(); ++i) {
    TrackSegment seg;
    seg.type = boundaries[i].nextType;
    seg.startFrame = boundaries[i].frame;
    seg.endFrame = (i + 1 < boundaries.size()) ? boundaries[i + 1].frame : result.totalFrames;

    seg.startSec = frameToSec(seg.startFrame, sampleRate);
    seg.endSec = frameToSec(seg.endFrame, sampleRate);

    if (beatgrid && beatgrid->bpm > 20.0) {
      seg.startBar = calculateBarNumber(seg.startFrame, *beatgrid);
      seg.endBar = calculateBarNumber(seg.endFrame, *beatgrid);
    } else {
      seg.startBar = 1;
      seg.endBar = 1;
    }

    computeSegmentEnergies(seg, curve, stepSec);
    result.segments.push_back(seg);
  }

  return result;
}

std::vector<MixPointCue> StructureSegmenter::generateMixPointCues(
    const TrackStructure& structure) const {
  std::vector<MixPointCue> cues;
  if (structure.empty()) return cues;

  int nextSlot = 0;

  for (const auto& seg : structure.segments) {
    if (nextSlot >= 8) break;

    MixPointCue cue;
    cue.frame = seg.startFrame;
    cue.timeSec = seg.startSec;
    cue.hotCueSlot = nextSlot++;

    switch (seg.type) {
      case SegmentType::Intro:
        cue.name = "Intro";
        cue.color = "#00FF88";  // Neon green
        cue.cueType = "intro";
        cues.push_back(cue);
        break;

      case SegmentType::Drop:
        cue.name = "Drop 1";
        cue.color = "#FF3366";  // Red
        cue.cueType = "drop";
        cues.push_back(cue);
        break;

      case SegmentType::Break:
        cue.name = "Breakdown";
        cue.color = "#3399FF";  // Blue
        cue.cueType = "break";
        cues.push_back(cue);
        break;

      case SegmentType::Drop2:
        cue.name = "Drop 2";
        cue.color = "#FF0055";  // Pink / Red
        cue.cueType = "drop";
        cues.push_back(cue);
        break;

      case SegmentType::Outro:
        cue.name = "Outro";
        cue.color = "#FFCC00";  // Yellow
        cue.cueType = "outro";
        cues.push_back(cue);
        break;

      case SegmentType::Build:
        // Buildup itself does not consume a primary hot cue slot, or can be skipped
        --nextSlot;  // Don't advance slot for build
        break;
    }
  }

  return cues;
}

}  // namespace zyron::analysis
