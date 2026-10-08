// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

#include "Audio/Routing/CueRouter.hpp"
#include "support/AllocationGuard.hpp"

using namespace zyron::audio;
using namespace zyron::test;
using Catch::Matchers::WithinAbs;

TEST_CASE("CueRouter is strictly realtime safe", "[audio][routing][cue][rt]") {
  CueRouter router;
  router.prepare(48000.0);
  router.setMode(HeadphoneRoutingMode::MultiChannel);

  std::vector<float> mL(128, 0.5F);
  std::vector<float> mR(128, 0.5F);
  std::vector<float> cL(128, 0.3F);
  std::vector<float> cR(128, 0.3F);

  std::vector<float> ch0(128);
  std::vector<float> ch1(128);
  std::vector<float> ch2(128);
  std::vector<float> ch3(128);
  float* outputs[4] = {ch0.data(), ch1.data(), ch2.data(), ch3.data()};

  {
    ScopedRealtimeGuard rtGuard;
    router.route(mL.data(), mR.data(), cL.data(), cR.data(), outputs, 4, 128);
  }

  CHECK(ch0[127] > 0.0F);
  CHECK(ch2[127] > 0.0F);
}

TEST_CASE("CueRouter MultiChannel routing and headphone mix", "[audio][routing][cue]") {
  CueRouter router;
  router.prepare(48000.0);
  router.setMode(HeadphoneRoutingMode::MultiChannel);

  std::vector<float> mL(256, 0.8F);
  std::vector<float> mR(256, 0.8F);
  std::vector<float> cL(256, 0.4F);
  std::vector<float> cR(256, 0.4F);

  std::vector<float> ch0(256);
  std::vector<float> ch1(256);
  std::vector<float> ch2(256);
  std::vector<float> ch3(256);
  float* outputs[4] = {ch0.data(), ch1.data(), ch2.data(), ch3.data()};

  SECTION("100% Cue in headphones (mix = 0.0)") {
    router.setHeadphoneMix(0.0F);
    router.setHeadphoneVolume(1.0F);
    router.reset();

    router.route(mL.data(), mR.data(), cL.data(), cR.data(), outputs, 4, 256);

    // Master on ch 0, 1
    CHECK_THAT(ch0[255], WithinAbs(0.8F, 1e-4F));
    CHECK_THAT(ch1[255], WithinAbs(0.8F, 1e-4F));

    // Headphones on ch 2, 3 receive Cue (0.4)
    CHECK_THAT(ch2[255], WithinAbs(0.4F, 1e-4F));
    CHECK_THAT(ch3[255], WithinAbs(0.4F, 1e-4F));
  }

  SECTION("100% Master in headphones (mix = 1.0)") {
    router.setHeadphoneMix(1.0F);
    router.setHeadphoneVolume(1.0F);
    router.reset();

    router.route(mL.data(), mR.data(), cL.data(), cR.data(), outputs, 4, 256);

    // Headphones on ch 2, 3 receive Master (0.8)
    CHECK_THAT(ch2[255], WithinAbs(0.8F, 1e-4F));
    CHECK_THAT(ch3[255], WithinAbs(0.8F, 1e-4F));
  }

  SECTION("50/50 blend (mix = 0.5)") {
    router.setHeadphoneMix(0.5F);
    router.setHeadphoneVolume(1.0F);
    router.reset();

    router.route(mL.data(), mR.data(), cL.data(), cR.data(), outputs, 4, 256);

    // 0.4 * 0.5 + 0.8 * 0.5 = 0.2 + 0.4 = 0.6
    CHECK_THAT(ch2[255], WithinAbs(0.6F, 1e-4F));
    CHECK_THAT(ch3[255], WithinAbs(0.6F, 1e-4F));
  }
}

TEST_CASE("CueRouter SplitCue fallback for 2-channel cards", "[audio][routing][cue][split_cue]") {
  CueRouter router;
  router.prepare(48000.0);
  router.setMode(HeadphoneRoutingMode::SplitCue);
  router.setHeadphoneVolume(1.0F);
  router.reset();

  std::vector<float> mL(128, 0.6F);
  std::vector<float> mR(128, 0.6F);
  std::vector<float> cL(128, 0.4F);
  std::vector<float> cR(128, 0.4F);

  std::vector<float> ch0(128);
  std::vector<float> ch1(128);
  float* outputs[2] = {ch0.data(), ch1.data()};

  router.route(mL.data(), mR.data(), cL.data(), cR.data(), outputs, 2, 128);

  // Left ear (ch 0) = Cue mono sum (0.4)
  CHECK_THAT(ch0[127], WithinAbs(0.4F, 1e-4F));

  // Right ear (ch 1) = Master mono sum (0.6)
  CHECK_THAT(ch1[127], WithinAbs(0.6F, 1e-4F));
}

TEST_CASE("CueRouter overwrites stale buffer contents with silence", "[audio][routing][cue][stale]") {
  CueRouter router;
  router.prepare(48000.0);
  router.setMode(HeadphoneRoutingMode::Disabled);

  std::vector<float> mL(64, 0.5F);
  std::vector<float> mR(64, 0.5F);

  // Channels filled with stale garbage
  std::vector<float> ch0(64, 99.0F);
  std::vector<float> ch1(64, 99.0F);
  std::vector<float> ch2(64, 99.0F);
  std::vector<float> ch3(64, 99.0F);
  float* outputs[4] = {ch0.data(), ch1.data(), ch2.data(), ch3.data()};

  router.route(mL.data(), mR.data(), nullptr, nullptr, outputs, 4, 64);

  CHECK_THAT(ch0[63], WithinAbs(0.5F, 1e-4F));
  CHECK_THAT(ch1[63], WithinAbs(0.5F, 1e-4F));
  // Channels 2 and 3 must be overwritten with silence (0.0F)
  CHECK_THAT(ch2[63], WithinAbs(0.0F, 1e-5F));
  CHECK_THAT(ch3[63], WithinAbs(0.0F, 1e-5F));
}
