// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "Audio/DSP/OutputStage.hpp"
#include "support/AllocationGuard.hpp"

using zyron::audio::OutputStage;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr float kStale = 7.0F;  // what a reused audio buffer might still hold from the previous block

void renderRtSafe(OutputStage& stage, float* const* channelData, int numChannels, int numSamples) {
  zyron::test::ScopedRealtimeGuard guard;
  stage.render(channelData, numChannels, numSamples);
  REQUIRE_FALSE(guard.hasViolations());
}

struct Buffers {
  Buffers(int channels, int samples)
      : data(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(samples), kStale)) {
    for (auto& channel : data) {
      pointers.push_back(channel.data());
    }
  }
  std::vector<std::vector<float>> data;
  std::vector<float*> pointers;
};

/// Prepares a stage that plays a full-scale 1 kHz tone (the stage holds atomics, so it cannot be returned by value).
void startPlaying(OutputStage& stage) {
  stage.prepare(kSampleRate);
  stage.tone().setFrequencyHz(1000.0F);
  stage.tone().setLevelDb(0.0F);
  stage.tone().setEnabled(true);
}

}  // namespace

TEST_CASE("every output channel receives the same signal") {
  for (const int channels : {1, 2, 4, 8}) {
    INFO(channels << " channels");
    OutputStage stage;
    startPlaying(stage);
    Buffers out(channels, 480);
    for (int block = 0; block < 4; ++block) {  // let the tone fade in so the buffers are not all zero
      renderRtSafe(stage, out.pointers.data(), channels, 480);
    }

    CHECK(*std::max_element(out.data[0].begin(), out.data[0].end()) > 0.1F);
    for (int channel = 1; channel < channels; ++channel) {
      CHECK(out.data[static_cast<std::size_t>(channel)] == out.data[0]);
    }
  }
}

TEST_CASE("stale buffer contents are always overwritten, even while the tone is off") {
  OutputStage stage;
  stage.prepare(kSampleRate);  // tone disabled
  Buffers out(2, 256);

  renderRtSafe(stage, out.pointers.data(), 2, 256);

  for (const auto& channel : out.data) {
    CHECK(std::all_of(channel.begin(), channel.end(), [](float s) { return s == 0.0F; }));
  }
}

TEST_CASE("a missing first channel zeroes the others instead of leaving stale data") {
  OutputStage stage;
  startPlaying(stage);
  Buffers out(3, 128);
  out.pointers[0] = nullptr;

  renderRtSafe(stage, out.pointers.data(), 3, 128);

  for (std::size_t channel = 1; channel < 3; ++channel) {
    CHECK(std::all_of(out.data[channel].begin(), out.data[channel].end(), [](float s) { return s == 0.0F; }));
  }
  CHECK(out.data[0].front() == kStale);  // the null channel's own buffer was never ours to touch
}

TEST_CASE("a missing later channel is skipped and the rest still play") {
  OutputStage stage;
  startPlaying(stage);
  Buffers out(3, 480);
  out.pointers[1] = nullptr;
  for (int block = 0; block < 4; ++block) {
    renderRtSafe(stage, out.pointers.data(), 3, 480);
  }

  CHECK(out.data[1].front() == kStale);
  CHECK(out.data[2] == out.data[0]);
}

TEST_CASE("degenerate calls change nothing and do not crash") {
  OutputStage stage;
  startPlaying(stage);
  Buffers out(2, 64);

  renderRtSafe(stage, nullptr, 2, 64);
  renderRtSafe(stage, out.pointers.data(), 0, 64);
  renderRtSafe(stage, out.pointers.data(), -1, 64);
  renderRtSafe(stage, out.pointers.data(), 2, 0);
  renderRtSafe(stage, out.pointers.data(), 2, -5);

  for (const auto& channel : out.data) {
    CHECK(std::all_of(channel.begin(), channel.end(), [](float s) { return s == kStale; }));
  }
}

TEST_CASE("the tone's fade-in survives block boundaries across channels") {
  OutputStage stage;
  startPlaying(stage);
  Buffers a(2, 100);
  Buffers b(2, 100);

  renderRtSafe(stage, a.pointers.data(), 2, 100);
  renderRtSafe(stage, b.pointers.data(), 2, 100);

  // The sine is continuous across the two blocks: the step between the last sample of one and the first of the next
  // is no bigger than inside a block (2*pi*1000/48000 = 0.131, plus the fade).
  CHECK(std::abs(b.data[0].front() - a.data[0].back()) < 0.15F);
}
