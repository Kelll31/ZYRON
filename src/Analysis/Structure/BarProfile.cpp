// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Structure/BarProfile.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace zyron::analysis {

namespace {

constexpr double kBassCutoffHz = 150.0;
constexpr double kReferencePercentile = 0.95;
constexpr double kSilenceRms = 1.0e-5;
constexpr int kQuantLevels = 255;
constexpr double kBarCountSlack = 1.0e-6;  // 24 bars of 1.41 s must not come out as 23.9999
constexpr const char* kMagic = "ZB1";

/// Maps bar RMS values to 0..1 relative to their loud reference, on a dB scale.
std::vector<float> normalise(const std::vector<double>& rms) {
  std::vector<double> sorted = rms;
  std::sort(sorted.begin(), sorted.end());
  const double reference =
      sorted.empty() ? 0.0 : sorted[static_cast<std::size_t>(kReferencePercentile * static_cast<double>(sorted.size() - 1))];
  std::vector<float> out(rms.size(), 0.0F);
  if (reference <= kSilenceRms) {
    return out;
  }
  for (std::size_t i = 0; i < rms.size(); ++i) {
    if (rms[i] <= kSilenceRms) {
      continue;
    }
    const double db = 20.0 * std::log10(rms[i] / reference);
    out[i] = static_cast<float>(std::clamp(1.0 + db / BarProfile::kBarEnergyRangeDb, 0.0, 1.0));
  }
  return out;
}

void appendHex(std::string& text, const std::vector<float>& values) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  for (const float v : values) {
    const int q = std::clamp(static_cast<int>(std::lround(v * kQuantLevels)), 0, kQuantLevels);
    text.push_back(kDigits[q >> 4]);
    text.push_back(kDigits[q & 15]);
  }
}

bool parseHex(std::string_view text, std::size_t count, std::vector<float>& out) {
  if (text.size() != count * 2) {
    return false;
  }
  out.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    int q = 0;
    const auto [ptr, ec] = std::from_chars(text.data() + 2 * i, text.data() + 2 * i + 2, q, 16);
    if (ec != std::errc{} || ptr != text.data() + 2 * i + 2) {
      return false;
    }
    out[i] = static_cast<float>(q) / static_cast<float>(kQuantLevels);
  }
  return true;
}

bool parseDouble(std::string_view text, double& value) {
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  return ec == std::errc{} && ptr == text.data() + text.size() && std::isfinite(value);
}

}  // namespace

std::string BarProfile::encode() const {
  if (empty()) {
    return {};
  }
  char head[96];
  std::snprintf(head, sizeof(head), "%s;%.4f;%.6f;%zu;", kMagic, originSec, barSec, energy.size());
  std::string text = head;
  appendHex(text, energy);
  text.push_back(';');
  std::vector<float> b = bass;
  b.resize(energy.size(), 0.0F);
  appendHex(text, b);
  return text;
}

bool BarProfile::decode(std::string_view text, BarProfile& out) {
  std::vector<std::string_view> parts;
  while (true) {
    const auto cut = text.find(';');
    parts.push_back(text.substr(0, cut));
    if (cut == std::string_view::npos) {
      break;
    }
    text.remove_prefix(cut + 1);
  }
  if (parts.size() != 6 || parts[0] != kMagic) {
    return false;
  }
  BarProfile result;
  std::size_t count = 0;
  if (!parseDouble(parts[1], result.originSec) || !parseDouble(parts[2], result.barSec) || result.barSec <= 0.0 ||
      std::from_chars(parts[3].data(), parts[3].data() + parts[3].size(), count).ec != std::errc{} || count == 0 ||
      count > kMaxBars || !parseHex(parts[4], count, result.energy) || !parseHex(parts[5], count, result.bass)) {
    return false;
  }
  out = std::move(result);
  return true;
}

BarProfile computeBarProfile(const BarProfileInput& in) {
  BarProfile profile;
  if (in.mono == nullptr || in.frames == 0 || in.sampleRate <= 0 || in.barSec <= 0.0) {
    return profile;
  }
  const double rate = static_cast<double>(in.sampleRate);
  const double end = std::min(in.endSec, static_cast<double>(in.frames) / rate);
  const double span = end - in.originSec;
  if (span < in.barSec) {
    return profile;
  }
  const auto bars = std::min<std::size_t>(BarProfile::kMaxBars, static_cast<std::size_t>(span / in.barSec + kBarCountSlack));
  std::vector<double> full(bars, 0.0);
  std::vector<double> low(bars, 0.0);
  std::vector<double> counts(bars, 0.0);
  const double a = 1.0 - std::exp(-2.0 * std::numbers::pi * kBassCutoffHz / rate);
  double lp1 = 0.0;
  double lp2 = 0.0;
  const auto first = static_cast<std::size_t>(std::max(0.0, in.originSec * rate));
  const auto last = std::min(in.frames, static_cast<std::size_t>((in.originSec + in.barSec * static_cast<double>(bars)) * rate));
  const double samplesPerBar = in.barSec * rate;
  std::size_t bar = 0;
  double barEnd = static_cast<double>(first) + samplesPerBar;
  for (std::size_t i = first; i < last; ++i) {
    while (static_cast<double>(i) >= barEnd && bar + 1 < bars) {
      ++bar;
      barEnd += samplesPerBar;
    }
    const double x = static_cast<double>(in.mono[i]);
    lp1 += a * (x - lp1);
    lp2 += a * (lp1 - lp2);
    full[bar] += x * x;
    low[bar] += lp2 * lp2;
    counts[bar] += 1.0;
  }
  for (std::size_t b = 0; b < bars; ++b) {
    full[b] = counts[b] > 0.0 ? std::sqrt(full[b] / counts[b]) : 0.0;
    low[b] = counts[b] > 0.0 ? std::sqrt(low[b] / counts[b]) : 0.0;
  }
  profile.originSec = in.originSec;
  profile.barSec = in.barSec;
  profile.energy = normalise(full);
  profile.bass = normalise(low);
  return profile;
}

}  // namespace zyron::analysis
