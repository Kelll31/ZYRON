// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Waveform/WaveformPeaks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <numbers>

namespace zyron::analysis {

namespace {

// 1-pole state variable / RC filter for fast 3-band energy decomposition
struct SimpleFilter {
  float y1{0.0f};
  float a{0.0f};

  void setCutoff(float fc, float sampleRate) noexcept {
    const float dt = 1.0f / sampleRate;
    const float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * fc);
    a = dt / (rc + dt);
  }

  inline float process(float x) noexcept {
    y1 += a * (x - y1);
    return y1;
  }
};

struct ThreeBandSplitter {
  SimpleFilter lpLow;
  SimpleFilter lpMid;

  void init(float sampleRate) {
    lpLow.setCutoff(250.0f, sampleRate);
    lpMid.setCutoff(2500.0f, sampleRate);
  }

  inline void process(float in, float& low, float& mid, float& high) noexcept {
    low = lpLow.process(in);
    const float lowMid = lpMid.process(in);
    mid = lowMid - low;
    high = in - lowMid;
  }
};

}  // namespace

void WaveformPeaks::buildOverview(int factor) {
  overviewFrames_.clear();
  if (detailFrames_.empty() || factor <= 0) return;

  const std::size_t numDetail = detailFrames_.size();
  const std::size_t numOverview = (numDetail + factor - 1) / factor;
  overviewFrames_.reserve(numOverview);

  for (std::size_t i = 0; i < numDetail; i += factor) {
    const std::size_t end = std::min(numDetail, i + factor);
    WaveformFrame ov;
    ov.minLeft = 1.0f;
    ov.maxLeft = -1.0f;
    ov.minRight = 1.0f;
    ov.maxRight = -1.0f;

    float lowSum = 0.0f;
    float midSum = 0.0f;
    float highSum = 0.0f;

    for (std::size_t j = i; j < end; ++j) {
      const auto& df = detailFrames_[j];
      ov.minLeft = std::min(ov.minLeft, df.minLeft);
      ov.maxLeft = std::max(ov.maxLeft, df.maxLeft);
      ov.minRight = std::min(ov.minRight, df.minRight);
      ov.maxRight = std::max(ov.maxRight, df.maxRight);
      lowSum = std::max(lowSum, df.lowEnergy);
      midSum = std::max(midSum, df.midEnergy);
      highSum = std::max(highSum, df.highEnergy);
    }

    ov.lowEnergy = lowSum;
    ov.midEnergy = midSum;
    ov.highEnergy = highSum;
    overviewFrames_.push_back(ov);
  }
}

bool WaveformPeaks::saveToFile(const std::filesystem::path& path, std::string* errorOut) const {
  std::ofstream f(path, std::ios::binary);
  if (!f.is_open()) {
    if (errorOut) *errorOut = "Failed to open output file: " + path.string();
    return false;
  }

  // Header structure (32 bytes)
  struct FileHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t sampleRate;
    std::uint16_t channels;
    std::uint16_t samplesPerFrame;
    std::uint32_t detailFramesCount;
    std::uint32_t overviewFramesCount;
    std::uint32_t reserved[2];
  } header{};

  header.magic = kMagic;
  header.version = kCurrentVersion;
  header.sampleRate = static_cast<std::uint32_t>(sampleRate_);
  header.channels = static_cast<std::uint16_t>(channels_);
  header.samplesPerFrame = static_cast<std::uint16_t>(samplesPerFrame_);
  header.detailFramesCount = static_cast<std::uint32_t>(detailFrames_.size());
  header.overviewFramesCount = static_cast<std::uint32_t>(overviewFrames_.size());

  f.write(reinterpret_cast<const char*>(&header), sizeof(header));

  if (!detailFrames_.empty()) {
    f.write(reinterpret_cast<const char*>(detailFrames_.data()),
            static_cast<std::streamsize>(detailFrames_.size() * sizeof(WaveformFrame)));
  }

  if (!overviewFrames_.empty()) {
    f.write(reinterpret_cast<const char*>(overviewFrames_.data()),
            static_cast<std::streamsize>(overviewFrames_.size() * sizeof(WaveformFrame)));
  }

  return f.good();
}

std::optional<WaveformPeaks> WaveformPeaks::loadFromFile(const std::filesystem::path& path,
                                                         std::string* errorOut) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) {
    if (errorOut) *errorOut = "Failed to open input file: " + path.string();
    return std::nullopt;
  }

  struct FileHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t sampleRate;
    std::uint16_t channels;
    std::uint16_t samplesPerFrame;
    std::uint32_t detailFramesCount;
    std::uint32_t overviewFramesCount;
    std::uint32_t reserved[2];
  } header{};

  if (!f.read(reinterpret_cast<char*>(&header), sizeof(header))) {
    if (errorOut) *errorOut = "Failed to read header: " + path.string();
    return std::nullopt;
  }

  if (header.magic != kMagic || header.version != kCurrentVersion) {
    if (errorOut) *errorOut = "Invalid magic or version in waveform file: " + path.string();
    return std::nullopt;
  }

  WaveformPeaks peaks;
  peaks.setAttributes(static_cast<int>(header.sampleRate),
                      static_cast<int>(header.channels),
                      static_cast<int>(header.samplesPerFrame));

  if (header.detailFramesCount > 0) {
    std::vector<WaveformFrame> df(header.detailFramesCount);
    if (!f.read(reinterpret_cast<char*>(df.data()),
                static_cast<std::streamsize>(header.detailFramesCount * sizeof(WaveformFrame)))) {
      if (errorOut) *errorOut = "Corrupted detail frames: " + path.string();
      return std::nullopt;
    }
    peaks.setDetailFrames(std::move(df));
  }

  if (header.overviewFramesCount > 0) {
    std::vector<WaveformFrame> of(header.overviewFramesCount);
    if (!f.read(reinterpret_cast<char*>(of.data()),
                static_cast<std::streamsize>(header.overviewFramesCount * sizeof(WaveformFrame)))) {
      if (errorOut) *errorOut = "Corrupted overview frames: " + path.string();
      return std::nullopt;
    }
    peaks.setOverviewFrames(std::move(of));
  }

  return peaks;
}

WaveformPeaks WaveformGenerator::generate(const float* const* channelData,
                                          int numChannels,
                                          std::size_t numSamples,
                                          int sampleRate,
                                          int samplesPerFrame) {
  WaveformPeaks peaks;
  peaks.setAttributes(sampleRate, numChannels, samplesPerFrame);

  if (channelData == nullptr || numSamples == 0 || samplesPerFrame <= 0) {
    return peaks;
  }

  const float* left = (numChannels > 0 && channelData[0] != nullptr) ? channelData[0] : nullptr;
  const float* right = (numChannels > 1 && channelData[1] != nullptr) ? channelData[1] : left;

  ThreeBandSplitter splitter;
  splitter.init(static_cast<float>(sampleRate));

  const std::size_t totalFrames = (numSamples + samplesPerFrame - 1) / samplesPerFrame;
  std::vector<WaveformFrame> frames;
  frames.reserve(totalFrames);

  for (std::size_t frameIdx = 0; frameIdx < numSamples; frameIdx += samplesPerFrame) {
    const std::size_t count = std::min<std::size_t>(samplesPerFrame, numSamples - frameIdx);

    float minL = 0.0f;
    float maxL = 0.0f;
    float minR = 0.0f;
    float maxR = 0.0f;
    float lowSq = 0.0f;
    float midSq = 0.0f;
    float highSq = 0.0f;

    for (std::size_t i = 0; i < count; ++i) {
      const float sL = left ? left[frameIdx + i] : 0.0f;
      const float sR = right ? right[frameIdx + i] : sL;

      minL = std::min(minL, sL);
      maxL = std::max(maxL, sL);
      minR = std::min(minR, sR);
      maxR = std::max(maxR, sR);

      const float mono = 0.5f * (sL + sR);
      float low = 0.0f, mid = 0.0f, high = 0.0f;
      splitter.process(mono, low, mid, high);

      lowSq += low * low;
      midSq += mid * mid;
      highSq += high * high;
    }

    WaveformFrame wf;
    wf.minLeft = minL;
    wf.maxLeft = maxL;
    wf.minRight = minR;
    wf.maxRight = maxR;

    const float invCount = 1.0f / static_cast<float>(count);
    wf.lowEnergy = std::clamp(std::sqrt(lowSq * invCount), 0.0f, 1.0f);
    wf.midEnergy = std::clamp(std::sqrt(midSq * invCount), 0.0f, 1.0f);
    wf.highEnergy = std::clamp(std::sqrt(highSq * invCount), 0.0f, 1.0f);

    frames.push_back(wf);
  }

  peaks.setDetailFrames(std::move(frames));
  peaks.buildOverview();
  return peaks;
}

WaveformPeaks WaveformGenerator::generateInterleaved(const float* interleavedData,
                                                     int numChannels,
                                                     std::size_t numFrames,
                                                     int sampleRate,
                                                     int samplesPerFrame) {
  WaveformPeaks peaks;
  peaks.setAttributes(sampleRate, numChannels, samplesPerFrame);

  if (interleavedData == nullptr || numFrames == 0 || samplesPerFrame <= 0 || numChannels <= 0) {
    return peaks;
  }

  ThreeBandSplitter splitter;
  splitter.init(static_cast<float>(sampleRate));

  const std::size_t totalBlocks = (numFrames + samplesPerFrame - 1) / samplesPerFrame;
  std::vector<WaveformFrame> frames;
  frames.reserve(totalBlocks);

  for (std::size_t blockIdx = 0; blockIdx < numFrames; blockIdx += samplesPerFrame) {
    const std::size_t count = std::min<std::size_t>(samplesPerFrame, numFrames - blockIdx);

    float minL = 0.0f;
    float maxL = 0.0f;
    float minR = 0.0f;
    float maxR = 0.0f;
    float lowSq = 0.0f;
    float midSq = 0.0f;
    float highSq = 0.0f;

    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t sampleOffset = (blockIdx + i) * numChannels;
      const float sL = interleavedData[sampleOffset];
      const float sR = (numChannels > 1) ? interleavedData[sampleOffset + 1] : sL;

      minL = std::min(minL, sL);
      maxL = std::max(maxL, sL);
      minR = std::min(minR, sR);
      maxR = std::max(maxR, sR);

      const float mono = 0.5f * (sL + sR);
      float low = 0.0f, mid = 0.0f, high = 0.0f;
      splitter.process(mono, low, mid, high);

      lowSq += low * low;
      midSq += mid * mid;
      highSq += high * high;
    }

    WaveformFrame wf;
    wf.minLeft = minL;
    wf.maxLeft = maxL;
    wf.minRight = minR;
    wf.maxRight = maxR;

    const float invCount = 1.0f / static_cast<float>(count);
    wf.lowEnergy = std::clamp(std::sqrt(lowSq * invCount), 0.0f, 1.0f);
    wf.midEnergy = std::clamp(std::sqrt(midSq * invCount), 0.0f, 1.0f);
    wf.highEnergy = std::clamp(std::sqrt(highSq * invCount), 0.0f, 1.0f);

    frames.push_back(wf);
  }

  peaks.setDetailFrames(std::move(frames));
  peaks.buildOverview();
  return peaks;
}

core::WaveformData WaveformPeaks::toWaveformData() const {
  core::WaveformData data;
  data.sampleRate = sampleRate_;
  data.channels = channels_;
  data.samplesPerFrame = samplesPerFrame_;

  data.detail.reserve(detailFrames_.size());
  for (const auto& f : detailFrames_) {
    data.detail.push_back({f.minLeft, f.maxLeft, f.minRight, f.maxRight, f.lowEnergy, f.midEnergy, f.highEnergy});
  }

  data.overview.reserve(overviewFrames_.size());
  for (const auto& f : overviewFrames_) {
    data.overview.push_back({f.minLeft, f.maxLeft, f.minRight, f.maxRight, f.lowEnergy, f.midEnergy, f.highEnergy});
  }

  return data;
}

}  // namespace zyron::analysis
