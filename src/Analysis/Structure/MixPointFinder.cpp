// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Structure/MixPointFinder.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace zyron::analysis {

namespace {

constexpr double kWindowSec = 0.1;        // loudness resolution for the audible range
constexpr double kSilenceDb = -40.0;      // below the loud reference = silence (a fade tail, an empty minute)
constexpr double kBassCutoffHz = 150.0;   // the drop is where the bassline arrives
constexpr double kLoudShare = 0.6;        // of the 80th percentile of bar bass energy
constexpr int kPhraseBars = 8;            // drum & bass, house, techno: 8-bar phrases
constexpr int kSnapToleranceBars = 2;     // how far a section edge may move to land on a phrase
constexpr int kSustainBars = 4;           // a drop is loud for at least this long
constexpr int kBreakdownBars = 8;         // quiet bars that end a drop section
constexpr double kNoTempoTailSec = 30.0;  // without a grid, mix out this long before the sound ends

double percentile(std::vector<double> values, double share) {
  if (values.empty()) {
    return 0.0;
  }
  const auto index = static_cast<std::size_t>(share * static_cast<double>(values.size() - 1));
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
  return values[index];
}

/// Audible range from short-window RMS, relative to the track's own loud level.
void findAudibleRange(const MixPointInput& in, MixPoints& out) {
  const double rate = static_cast<double>(in.sampleRate);
  const auto window = std::max<std::size_t>(1, static_cast<std::size_t>(kWindowSec * rate));
  std::vector<double> rms;
  rms.reserve(in.frames / window + 1);
  for (std::size_t pos = 0; pos < in.frames; pos += window) {
    const std::size_t n = std::min(window, in.frames - pos);
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      const double v = in.mono[pos + i];
      sum += v * v;
    }
    rms.push_back(std::sqrt(sum / static_cast<double>(n)));
  }
  const double duration = static_cast<double>(in.frames) / rate;
  const double reference = percentile(rms, 0.95);
  if (reference <= 1.0e-6) {
    out.audibleEndSec = 0.0;  // all silence: no audible range, nothing to mix
    return;
  }
  const double floor = reference * std::pow(10.0, kSilenceDb / 20.0);
  std::size_t first = 0;
  while (first < rms.size() && rms[first] < floor) {
    ++first;
  }
  std::size_t last = rms.size();
  while (last > first && rms[last - 1] < floor) {
    --last;
  }
  out.audibleStartSec = static_cast<double>(first * window) / rate;
  out.audibleEndSec = std::min(duration, static_cast<double>(last * window) / rate);
}

/// Mean energy below kBassCutoffHz of each bar from `originSec` on.
std::vector<double> barBassEnergy(const MixPointInput& in, double originSec, double barSec, int bars) {
  const double rate = static_cast<double>(in.sampleRate);
  const double a = 1.0 - std::exp(-2.0 * std::numbers::pi * kBassCutoffHz / rate);
  std::vector<double> energy(static_cast<std::size_t>(bars), 0.0);
  std::vector<double> counts(static_cast<std::size_t>(bars), 0.0);
  double lp1 = 0.0;
  double lp2 = 0.0;
  const auto start = static_cast<std::size_t>(std::max(0.0, originSec * rate));
  const auto end = std::min(in.frames, static_cast<std::size_t>((originSec + barSec * bars) * rate));
  for (std::size_t i = start; i < end; ++i) {
    lp1 += a * (static_cast<double>(in.mono[i]) - lp1);
    lp2 += a * (lp1 - lp2);
    const auto bar = static_cast<std::size_t>((static_cast<double>(i) / rate - originSec) / barSec);
    if (bar < energy.size()) {
      energy[bar] += lp2 * lp2;
      counts[bar] += 1.0;
    }
  }
  for (std::size_t b = 0; b < energy.size(); ++b) {
    energy[b] = counts[b] > 0.0 ? std::sqrt(energy[b] / counts[b]) : 0.0;
  }
  return energy;
}

/// The nearest phrase boundary to `bar`, phrases counted from `anchor` (the first drop: tracks are written in 8-bar
/// phrases from there). Only moved when close; a section edge far from any phrase stays where it is.
int snapToPhrase(int bar, int anchor = 0) {
  const int nearest =
      anchor + static_cast<int>(std::lround(static_cast<double>(bar - anchor) / kPhraseBars)) * kPhraseBars;
  return std::abs(nearest - bar) <= kSnapToleranceBars ? nearest : bar;
}

}  // namespace

MixPoints findMixPoints(const MixPointInput& in) {
  MixPoints out;
  if (in.mono == nullptr || in.frames == 0 || in.sampleRate <= 0) {
    return out;
  }
  findAudibleRange(in, out);
  if (out.audibleEndSec <= out.audibleStartSec) {
    return out;
  }

  if (in.bpm <= 0.0) {
    out.mixInSec = out.audibleStartSec;
    out.mixOutSec = std::max(out.audibleStartSec, out.audibleEndSec - kNoTempoTailSec);
    return out;
  }

  // Bars are counted from the first beat of the music: tracks are written in phrases from there.
  const double beatSec = 60.0 / in.bpm;
  const double barSec = 4.0 * beatSec;
  const double tolerance = 0.25 * beatSec;  // a soft attack starts a little before its beat
  double origin = in.firstBeatSec + std::ceil((out.audibleStartSec - tolerance - in.firstBeatSec) / beatSec) * beatSec;
  if (origin < 0.0) {
    origin += beatSec * std::ceil(-origin / beatSec);
  }
  out.mixInSec = origin;

  const double transitionSec = in.transitionBeats * beatSec;
  double latestMixOut = out.audibleEndSec - transitionSec;  // the whole transition must still have sound
  const int bars = static_cast<int>((out.audibleEndSec - origin) / barSec);
  const auto toSec = [&](int bar) { return origin + bar * barSec; };
  if (bars < 2 * kPhraseBars) {
    out.mixOutSec = std::max(origin, latestMixOut);
    return out;
  }

  const std::vector<double> bass = barBassEnergy(in, origin, barSec, bars);
  const double loud = kLoudShare * percentile(bass, 0.8);
  const auto isLoud = [&](int bar) { return bass[static_cast<std::size_t>(bar)] >= loud; };
  const auto sustained = [&](int bar) {
    const int end = std::min(bars, bar + kSustainBars);
    double sum = 0.0;
    for (int b = bar; b < end; ++b) {
      sum += bass[static_cast<std::size_t>(b)];
    }
    return end > bar && sum / (end - bar) >= loud;
  };

  // Loud sections: runs of loud bars (a single quiet bar inside does not split one).
  struct Section {
    int start;
    int end;  // exclusive
  };
  std::vector<Section> sections;
  for (int bar = 0; bar < bars; ++bar) {
    if (!isLoud(bar)) {
      continue;
    }
    if (!sections.empty() && bar - sections.back().end <= 1) {
      sections.back().end = bar + 1;
    } else {
      sections.push_back({bar, bar + 1});
    }
  }

  // Drops: every loud section that follows a quiet stretch (the intro or a breakdown), short ones included: a short
  // "micro drop" at the end of a track must be known so that no mix runs into it. The first drop the AI mixes into is
  // the first section that holds for kSustainBars.
  int previousEnd = -kSustainBars;
  int phraseAnchor = 0;  // the first drop's bar: phrases are counted from it
  for (const Section& section : sections) {
    const int length = section.end - section.start;
    if (section.start - previousEnd >= kSustainBars && length >= 2) {
      const int dropBar = snapToPhrase(section.start);
      const double time = toSec(dropBar);
      out.drops.push_back(time);
      if (out.dropSec < 0.0 && length >= kSustainBars && sustained(section.start)) {
        out.dropSec = time;
        phraseAnchor = dropBar;
      }
    }
    previousEnd = section.end;
  }
  if (out.dropSec < 0.0 && !out.drops.empty()) {
    out.dropSec = out.drops.front();
  }

  // Mix out: where the last main section (16+ bars) ends, on a phrase. Anything loud after it (a micro drop, a reprise)
  // must not be reached during the transition: the mix out then moves earlier so the whole transition fits before it.
  const Section* lastMain = nullptr;
  for (const Section& section : sections) {
    if (section.end - section.start >= 2 * kPhraseBars) {
      lastMain = &section;
    }
  }
  if (lastMain == nullptr && !sections.empty()) {
    lastMain = &*std::max_element(sections.begin(), sections.end(), [](const Section& a, const Section& b) {
      return a.end - a.start < b.end - b.start;
    });
  }
  // A track that stays loud to the very end (no outro) is mixed out a whole phrase before its last bars, so it never
  // sounds cut off.
  if (lastMain != nullptr && lastMain->end >= bars - 2) {
    latestMixOut -= kPhraseBars * barSec;
  }
  const auto phraseAtOrBefore = [&](double sec) {
    const int bar = static_cast<int>(std::floor((sec - origin) / barSec + 1.0e-6));
    const int phrases = static_cast<int>(std::floor(static_cast<double>(bar - phraseAnchor) / kPhraseBars));
    return toSec(phraseAnchor + phrases * kPhraseBars);
  };
  double mixOut = lastMain != nullptr ? toSec(snapToPhrase(lastMain->end, phraseAnchor)) : latestMixOut;
  if (lastMain != nullptr) {
    for (const Section& section : sections) {
      const double later = toSec(section.start);
      if (section.start >= lastMain->end && later - mixOut < transitionSec) {
        const double earliest = toSec(lastMain->start + kPhraseBars);
        const double fit = phraseAtOrBefore(later - transitionSec);
        mixOut = std::max(earliest, fit);
        break;
      }
    }
  }
  if (mixOut > latestMixOut) {
    mixOut = phraseAtOrBefore(latestMixOut);  // on a phrase, before the sound runs out
  }
  if (out.dropSec > 0.0 && mixOut <= out.dropSec) {
    mixOut = std::min(latestMixOut, out.dropSec + kPhraseBars * 2 * barSec);  // never mix out before the track got going
  }
  out.mixOutSec = std::max(origin, mixOut);
  return out;
}

}  // namespace zyron::analysis
