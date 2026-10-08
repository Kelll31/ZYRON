// SPDX-License-Identifier: AGPL-3.0-only
#include "Application/StemService.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "Analysis/Features/Resampler.hpp"
#include "Stems/Demucs/DemucsStemSeparator.hpp"

namespace zyron::application {

namespace {

constexpr int kModelRate = 44100;
constexpr float kSeparationShare = 0.95F;  // the last 5 % of the progress bar is converting and handing over

std::string modelName() {
  return stems::DemucsStemSeparator{}.modelName();
}

std::string modelVersion() {
  return stems::DemucsStemSeparator{}.modelVersion();
}

/// One channel of a track buffer, resampled to `rate` when it differs.
std::vector<float> channelAt(const audio::TrackBuffer& buffer, int channel, int rate) {
  const float* data = buffer.channelData(std::min(channel, buffer.numChannels() - 1));
  const auto frames = static_cast<std::size_t>(buffer.numFrames());
  const int sourceRate = static_cast<int>(buffer.sampleRate());
  if (sourceRate == rate) {
    return std::vector<float>(data, data + frames);
  }
  return analysis::AudioResampler::resample(data, frames, sourceRate, rate);
}

}  // namespace

StemService::StemService(audio::AudioEngine& engine, library::LibraryService* library, NeuralModels& models,
                         const std::filesystem::path& cacheDirectory)
    : engine_(engine), library_(library), models_(models), cache_(cacheDirectory) {
  worker_ = std::thread(&StemService::workerLoop, this);
}

StemService::~StemService() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void StemService::request(core::DeckId deck, core::TrackId track) {
  engine_.setStemStatus(deck, track, core::StemPhase::Queued, 0.0F, {});
  enqueue(Job{deck, track, false});
}

void StemService::trackReady(core::DeckId deck, core::TrackId track) {
  if (cache_.hasStems(cacheKey(track), modelName(), modelVersion())) {
    enqueue(Job{deck, track, true});
  }
}

void StemService::enqueue(const Job& job) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(job);
  }
  cv_.notify_one();
}

std::string StemService::cacheKey(core::TrackId track) const {
  if (library_ != nullptr) {
    if (const auto item = library_->findTrack(track.value); item.has_value() && !item->contentHash.empty()) {
      return item->contentHash;
    }
  }
  return "track-" + std::to_string(track.value);
}

void StemService::workerLoop() {
  for (;;) {
    Job job{};
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) {
        return;
      }
      job = queue_.front();
      queue_.pop_front();
    }
    try {
      process(job);
    } catch (const std::exception& error) {
      engine_.setStemStatus(job.deck, job.track, core::StemPhase::Failed, 0.0F, error.what());
    }
  }
}

void StemService::process(const Job& job) {
  const auto fail = [&](const std::string& message) {
    engine_.setStemStatus(job.deck, job.track, core::StemPhase::Failed, 0.0F, message);
  };
  const std::string key = cacheKey(job.track);

  if (auto cached = cache_.loadStems(key, modelName(), modelVersion())) {
    if (!job.cachedOnly) {
      engine_.setStemStatus(job.deck, job.track, core::StemPhase::Running, kSeparationShare, {});
    }
    if (!attach(job, *cached) && !job.cachedOnly) {
      fail("The track on the deck changed before the stems were ready");
    }
    return;
  }
  if (job.cachedOnly) {
    return;  // nothing cached after all: stay silent, the user can still press SPLIT
  }

  engine_.setStemStatus(job.deck, job.track, core::StemPhase::Running, 0.0F, {});
  const std::shared_ptr<const audio::TrackBuffer> input = engine_.deckBuffer(job.deck);
  if (input == nullptr || input->numFrames() <= 0) {
    fail("There is no track on the deck");
    return;
  }

  std::string error;
  const std::shared_ptr<ai::IModelSession> session = models_.demucs(&error);
  if (session == nullptr) {
    fail(error);
    return;
  }

  std::vector<float> left = channelAt(*input, 0, kModelRate);
  std::vector<float> right = channelAt(*input, 1, kModelRate);
  const float* channels[2] = {left.data(), right.data()};

  stems::DemucsStemSeparator separator(session);
  stems::StemSeparationResult result = separator.separate(
      channels, 2, static_cast<std::int64_t>(left.size()), static_cast<double>(kModelRate), [&](float progress) {
        engine_.setStemStatus(job.deck, job.track, core::StemPhase::Running, progress * kSeparationShare, {});
      });
  left = {};
  right = {};

  if (!result.success) {
    fail(result.error);
    return;
  }
  (void)cache_.storeStems(key, modelName(), modelVersion(), result);  // not fatal if the disk refuses
  if (!attach(job, result)) {
    fail("The track on the deck changed before the stems were ready");
  }
}

bool StemService::attach(const Job& job, stems::StemSeparationResult& result) {
  const std::shared_ptr<const audio::TrackBuffer> reference = engine_.deckBuffer(job.deck);
  if (reference == nullptr) {
    return false;
  }
  const std::int64_t frames = reference->numFrames();
  const double rate = reference->sampleRate();

  std::array<std::shared_ptr<const audio::TrackBuffer>, core::kStemKindCount> buffers;
  for (std::size_t s = 0; s < stems::kStemCount; ++s) {
    stems::StemBuffer& stem = result.stems[s];
    std::vector<float> left = std::move(stem.left);
    std::vector<float> right = std::move(stem.right);
    stem.left = {};
    stem.right = {};
    if (static_cast<int>(rate) != kModelRate) {
      left = analysis::AudioResampler::resample(left.data(), left.size(), kModelRate, static_cast<int>(rate));
      right = analysis::AudioResampler::resample(right.data(), right.size(), kModelRate, static_cast<int>(rate));
    }
    // The stems must be exactly as long as the track: the deck plays both from one playhead.
    auto buffer = std::make_shared<audio::TrackBuffer>(2, frames, rate);
    const auto copied = static_cast<std::size_t>(std::min<std::int64_t>(frames, static_cast<std::int64_t>(left.size())));
    std::copy_n(left.data(), copied, buffer->channelData(0));
    std::copy_n(right.data(), std::min(copied, right.size()), buffer->channelData(1));
    buffers[s] = std::move(buffer);
  }
  return engine_.attachStems(job.deck, job.track, reference, std::move(buffers));
}

}  // namespace zyron::application
