// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

#include "Audio/Routing/StemRouter.hpp"
#include "Core/State/Ids.hpp"
#include "support/AllocationGuard.hpp"

using namespace zyron::audio;
using namespace zyron::core;
using namespace zyron::test;
using Catch::Matchers::WithinAbs;

TEST_CASE("StemRouter: default routing and state queries", "[audio][stems][routing]") {
  StemRouter router;

  // By default, Deck A stems route to default Ch 0, Deck B to Ch 1
  for (std::size_t s = 0; s < kStemKindCount; ++s) {
    const auto stem = static_cast<StemKind>(s);
    auto destA = router.route(DeckId::A, stem);
    CHECK(destA.type == StemRouteDestination::Type::DefaultDeckChannel);
    CHECK(destA.channelIndex == 0);

    auto destB = router.route(DeckId::B, stem);
    CHECK(destB.type == StemRouteDestination::Type::DefaultDeckChannel);
    CHECK(destB.channelIndex == 1);
  }
}

TEST_CASE("StemRouter: custom channel routing and disable", "[audio][stems][routing]") {
  StemRouter router;

  // Explicit channel route
  router.setChannelRoute(DeckId::A, StemKind::Vocals, 2);
  auto destVocA = router.route(DeckId::A, StemKind::Vocals);
  CHECK(destVocA.type == StemRouteDestination::Type::MixerChannel);
  CHECK(destVocA.channelIndex == 2);

  // Disable a stem
  StemRouteDestination disabledDest{StemRouteDestination::Type::Disabled, 0};
  router.setRoute(DeckId::A, StemKind::Other, disabledDest);
  CHECK(router.route(DeckId::A, StemKind::Other).type == StemRouteDestination::Type::Disabled);

  // Reset restores default
  router.reset();
  CHECK(router.route(DeckId::A, StemKind::Vocals).type == StemRouteDestination::Type::DefaultDeckChannel);
  CHECK(router.route(DeckId::A, StemKind::Other).type == StemRouteDestination::Type::DefaultDeckChannel);
}

TEST_CASE("StemRouter: split preset routing and audio summing", "[audio][stems][routing]") {
  StemRouter router;
  router.applySplitPreset();

  // Verify preset destinations (§44)
  // Channel 0: Deck A Drums + Bass
  // Channel 1: Deck A Vocals + Other
  // Channel 2: Deck B Vocals
  // Channel 3: Deck B Drums + Bass + Other
  CHECK(router.route(DeckId::A, StemKind::Drums).channelIndex == 0);
  CHECK(router.route(DeckId::A, StemKind::Bass).channelIndex == 0);
  CHECK(router.route(DeckId::A, StemKind::Vocals).channelIndex == 1);
  CHECK(router.route(DeckId::A, StemKind::Other).channelIndex == 1);

  CHECK(router.route(DeckId::B, StemKind::Vocals).channelIndex == 2);
  CHECK(router.route(DeckId::B, StemKind::Drums).channelIndex == 3);
  CHECK(router.route(DeckId::B, StemKind::Bass).channelIndex == 3);
  CHECK(router.route(DeckId::B, StemKind::Other).channelIndex == 3);

  constexpr int kFrames = 64;
  std::vector<float> aVocL(kFrames, 0.10F), aVocR(kFrames, 0.15F);
  std::vector<float> aDrmL(kFrames, 0.20F), aDrmR(kFrames, 0.25F);
  std::vector<float> aBasL(kFrames, 0.05F), aBasR(kFrames, 0.08F);
  std::vector<float> aOthL(kFrames, 0.02F), aOthR(kFrames, 0.03F);

  std::vector<float> bVocL(kFrames, 0.30F), bVocR(kFrames, 0.35F);
  std::vector<float> bDrmL(kFrames, 0.12F), bDrmR(kFrames, 0.14F);
  std::vector<float> bBasL(kFrames, 0.06F), bBasR(kFrames, 0.07F);
  std::vector<float> bOthL(kFrames, 0.01F), bOthR(kFrames, 0.02F);

  const float* deckAStemL[kStemKindCount] = {aVocL.data(), aDrmL.data(), aBasL.data(), aOthL.data()};
  const float* deckAStemR[kStemKindCount] = {aVocR.data(), aDrmR.data(), aBasR.data(), aOthR.data()};
  const float* deckBStemL[kStemKindCount] = {bVocL.data(), bDrmL.data(), bBasL.data(), bOthL.data()};
  const float* deckBStemR[kStemKindCount] = {bVocR.data(), bDrmR.data(), bBasR.data(), bOthR.data()};

  const float* const* allDecksL[kDeckCount] = {deckAStemL, deckBStemL};
  const float* const* allDecksR[kDeckCount] = {deckAStemR, deckBStemR};

  std::vector<float> ch0L(kFrames, 99.0F), ch0R(kFrames, 99.0F);
  std::vector<float> ch1L(kFrames, 99.0F), ch1R(kFrames, 99.0F);
  std::vector<float> ch2L(kFrames, 99.0F), ch2R(kFrames, 99.0F);
  std::vector<float> ch3L(kFrames, 99.0F), ch3R(kFrames, 99.0F);

  float* outL[4] = {ch0L.data(), ch1L.data(), ch2L.data(), ch3L.data()};
  float* outR[4] = {ch0R.data(), ch1R.data(), ch2R.data(), ch3R.data()};

  router.routeStems(allDecksL, allDecksR, outL, outR, 4, kFrames);

  // Check channel sums
  // Ch 0 = Deck A Drums (0.20) + Bass (0.05) = 0.25
  CHECK_THAT(ch0L[0], WithinAbs(0.25F, 1e-5F));
  CHECK_THAT(ch0R[0], WithinAbs(0.33F, 1e-5F));

  // Ch 1 = Deck A Vocals (0.10) + Other (0.02) = 0.12
  CHECK_THAT(ch1L[0], WithinAbs(0.12F, 1e-5F));
  CHECK_THAT(ch1R[0], WithinAbs(0.18F, 1e-5F));

  // Ch 2 = Deck B Vocals (0.30)
  CHECK_THAT(ch2L[0], WithinAbs(0.30F, 1e-5F));
  CHECK_THAT(ch2R[0], WithinAbs(0.35F, 1e-5F));

  // Ch 3 = Deck B Drums (0.12) + Bass (0.06) + Other (0.01) = 0.19
  CHECK_THAT(ch3L[0], WithinAbs(0.19F, 1e-5F));
  CHECK_THAT(ch3R[0], WithinAbs(0.23F, 1e-5F));
}

TEST_CASE("StemRouter: realtime safe and robust against invalid inputs", "[audio][stems][routing]") {
  StemRouter router;

  constexpr int kFrames = 128;
  std::vector<float> sL(kFrames, 0.1F), sR(kFrames, 0.1F);
  const float* stemsL[kStemKindCount] = {sL.data(), sL.data(), sL.data(), sL.data()};
  const float* stemsR[kStemKindCount] = {sR.data(), sR.data(), sR.data(), sR.data()};
  const float* const* decksL[kDeckCount] = {stemsL, stemsR};
  const float* const* decksR[kDeckCount] = {stemsR, stemsR};

  std::vector<float> outL0(kFrames, 0.0F), outR0(kFrames, 0.0F);
  std::vector<float> outL1(kFrames, 0.0F), outR1(kFrames, 0.0F);
  float* outsL[2] = {outL0.data(), outL1.data()};
  float* outsR[2] = {outR0.data(), outR1.data()};

  // Zero allocations in audio thread
  {
    ScopedRealtimeGuard guard;
    router.routeStems(decksL, decksR, outsL, outsR, 2, kFrames);
    CHECK_FALSE(guard.hasViolations());
  }

  // Gracefully handles nullptrs and 0 frames
  router.routeStems(nullptr, nullptr, outsL, outsR, 2, kFrames);
  router.routeStems(decksL, decksR, nullptr, nullptr, 2, kFrames);
  router.routeStems(decksL, decksR, outsL, outsR, 0, kFrames);
  router.routeStems(decksL, decksR, outsL, outsR, 2, 0);
}
