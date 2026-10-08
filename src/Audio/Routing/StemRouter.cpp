// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Routing/StemRouter.hpp"

#include <algorithm>

namespace zyron::audio {

StemRouter::StemRouter() {
  applyDefaultRouting();
}

void StemRouter::reset() noexcept {
  applyDefaultRouting();
}

void StemRouter::setRoute(core::DeckId deck, core::StemKind stem, StemRouteDestination destination) noexcept {
  if (!core::isValid(deck) || !core::isValid(stem)) {
    return;
  }
  const auto d = core::index(deck);
  const auto s = core::index(stem);
  matrix_[d][s].type.store(destination.type, std::memory_order_relaxed);
  matrix_[d][s].channelIndex.store(destination.channelIndex, std::memory_order_relaxed);
}

void StemRouter::setChannelRoute(core::DeckId deck, core::StemKind stem, int mixerChannel) noexcept {
  StemRouteDestination dest;
  dest.type = StemRouteDestination::Type::MixerChannel;
  dest.channelIndex = std::clamp(mixerChannel, 0, kMaxMixerChannels - 1);
  setRoute(deck, stem, dest);
}

StemRouteDestination StemRouter::route(core::DeckId deck, core::StemKind stem) const noexcept {
  if (!core::isValid(deck) || !core::isValid(stem)) {
    return {};
  }
  const auto d = core::index(deck);
  const auto s = core::index(stem);

  StemRouteDestination dest;
  dest.type = matrix_[d][s].type.load(std::memory_order_relaxed);
  dest.channelIndex = matrix_[d][s].channelIndex.load(std::memory_order_relaxed);
  return dest;
}

void StemRouter::applyDefaultRouting() noexcept {
  for (std::size_t d = 0; d < core::kDeckCount; ++d) {
    const int defaultCh = static_cast<int>(std::min<std::size_t>(d, kMaxMixerChannels - 1));
    for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
      matrix_[d][s].type.store(StemRouteDestination::Type::DefaultDeckChannel, std::memory_order_relaxed);
      matrix_[d][s].channelIndex.store(defaultCh, std::memory_order_relaxed);
    }
  }
}

void StemRouter::applySplitPreset() noexcept {
  // SPEC section 44 preset:
  // Channel 0: Deck A Drums + Bass
  // Channel 1: Deck A Vocals + Other
  // Channel 2: Deck B Vocals
  // Channel 3: Deck B Drums + Bass + Other
  setChannelRoute(core::DeckId::A, core::StemKind::Drums, 0);
  setChannelRoute(core::DeckId::A, core::StemKind::Bass, 0);
  setChannelRoute(core::DeckId::A, core::StemKind::Vocals, 1);
  setChannelRoute(core::DeckId::A, core::StemKind::Other, 1);

  setChannelRoute(core::DeckId::B, core::StemKind::Vocals, 2);
  setChannelRoute(core::DeckId::B, core::StemKind::Drums, 3);
  setChannelRoute(core::DeckId::B, core::StemKind::Bass, 3);
  setChannelRoute(core::DeckId::B, core::StemKind::Other, 3);
}

void StemRouter::routeStems(const float* const* const* deckStemLefts, const float* const* const* deckStemRights,
                            float* const* channelLefts, float* const* channelRights, int numChannels,
                            int numSamples) noexcept {
  if (channelLefts == nullptr || channelRights == nullptr || numChannels <= 0 || numSamples <= 0) {
    return;
  }

  // Clear output channel buffers
  const int channelsToProcess = std::min(numChannels, kMaxMixerChannels);
  for (int ch = 0; ch < channelsToProcess; ++ch) {
    if (channelLefts[ch] != nullptr) {
      std::fill_n(channelLefts[ch], numSamples, 0.0F);
    }
    if (channelRights[ch] != nullptr) {
      std::fill_n(channelRights[ch], numSamples, 0.0F);
    }
  }

  if (deckStemLefts == nullptr || deckStemRights == nullptr) {
    return;
  }

  for (std::size_t d = 0; d < core::kDeckCount; ++d) {
    const auto* deckL = deckStemLefts[d];
    const auto* deckR = deckStemRights[d];
    if (deckL == nullptr || deckR == nullptr) {
      continue;
    }

    for (std::size_t s = 0; s < core::kStemKindCount; ++s) {
      const float* inL = deckL[s];
      const float* inR = deckR[s];
      if (inL == nullptr && inR == nullptr) {
        continue;
      }

      const auto routeType = matrix_[d][s].type.load(std::memory_order_relaxed);
      if (routeType == StemRouteDestination::Type::Disabled) {
        continue;
      }

      int targetChannel = 0;
      if (routeType == StemRouteDestination::Type::DefaultDeckChannel) {
        targetChannel = static_cast<int>(std::min<std::size_t>(d, channelsToProcess - 1));
      } else {
        targetChannel = matrix_[d][s].channelIndex.load(std::memory_order_relaxed);
      }

      if (targetChannel < 0 || targetChannel >= channelsToProcess) {
        continue;
      }

      float* outL = channelLefts[targetChannel];
      float* outR = channelRights[targetChannel];

      for (int i = 0; i < numSamples; ++i) {
        if (outL != nullptr && inL != nullptr) {
          outL[i] += inL[i];
        }
        if (outR != nullptr && inR != nullptr) {
          outR[i] += inR[i];
        }
      }
    }
  }
}

}  // namespace zyron::audio
