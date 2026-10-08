// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <memory>
#include <string>

#include "Core/Release/UpdateTypes.hpp"
#include "Platform/Release/UpdateChecker.hpp"

using namespace zyron;

namespace {

class MockHttpTransport : public core::IHttpTransport {
 public:
  HttpResponse responseToReturn;
  std::string lastQueriedUrl;
  int getCallCount{0};
  int downloadCallCount{0};

  HttpResponse get(const std::string& url, int /*timeoutMs*/) override {
    lastQueriedUrl = url;
    ++getCallCount;
    return responseToReturn;
  }

  bool downloadFile(const std::string& /*url*/,
                    const std::string& /*destinationPath*/,
                    std::function<void(float progress)> /*onProgress*/,
                    int /*timeoutMs*/) override {
    ++downloadCallCount;
    return true;
  }
};

}  // namespace

TEST_CASE("SemVer: semantic versioning logic", "[release][semver]") {
  SECTION("Valid version strings parsing") {
    const auto v1 = core::SemVer::parse("0.1.0");
    REQUIRE(v1.has_value());
    CHECK(v1->major == 0);
    CHECK(v1->minor == 1);
    CHECK(v1->patch == 0);
    CHECK(v1->prerelease.empty());
    CHECK(v1->toString() == "0.1.0");

    const auto v2 = core::SemVer::parse("v1.2.3-rc1+build123");
    REQUIRE(v2.has_value());
    CHECK(v2->major == 1);
    CHECK(v2->minor == 2);
    CHECK(v2->patch == 3);
    CHECK(v2->prerelease == "rc1");
    CHECK(v2->build == "build123");
  }

  SECTION("Invalid version strings return nullopt") {
    CHECK_FALSE(core::SemVer::parse("").has_value());
    CHECK_FALSE(core::SemVer::parse("1.0").has_value());
    CHECK_FALSE(core::SemVer::parse("invalid").has_value());
    CHECK_FALSE(core::SemVer::parse("-1.0.0").has_value());
  }

  SECTION("Precedence and comparison operators") {
    const auto v09 = *core::SemVer::parse("0.9.0");
    const auto v100 = *core::SemVer::parse("1.0.0");
    const auto v101 = *core::SemVer::parse("1.0.1");
    const auto v110 = *core::SemVer::parse("1.1.0");
    const auto v100rc = *core::SemVer::parse("1.0.0-rc1");

    CHECK(v09 < v100);
    CHECK(v100 < v101);
    CHECK(v101 < v110);
    CHECK(v100rc < v100);  // prerelease has lower precedence than final
    CHECK(v100 == *core::SemVer::parse("1.0.0"));
    CHECK(v101 >= v100);
  }
}

TEST_CASE("UpdateChecker: offline-first policy and network checks (SPEC section 75, P10-03)", "[release][update]") {
  const core::SemVer currentVer{0, 1, 0, "", ""};
  auto mockTransport = std::make_shared<MockHttpTransport>();
  platform::UpdateChecker checker(currentVer, mockTransport);

  SECTION("SPEC §75: Offline mode strictly blocks all HTTP queries") {
    const auto info = checker.checkUpdate(true);  // offlineMode = true
    CHECK(info.status == core::UpdateCheckStatus::OfflineModeBlocked);
    CHECK(mockTransport->getCallCount == 0);  // Zero network calls made!
    CHECK(mockTransport->lastQueriedUrl.empty());
  }

  SECTION("Update available detected when remote version is newer") {
    mockTransport->responseToReturn.statusCode = 200;
    mockTransport->responseToReturn.body = R"({
      "tag_name": "v0.2.0",
      "body": "Exciting new DJ features and stems performance upgrades",
      "html_url": "https://github.com/zyron-dj/zyron/releases/tag/v0.2.0",
      "published_at": "2026-10-08T12:00:00Z",
      "mandatory": false,
      "prerelease": false
    })";

    const auto info = checker.checkUpdate(false);  // online
    CHECK(info.status == core::UpdateCheckStatus::UpdateAvailable);
    CHECK(info.latestVersion == core::SemVer{0, 2, 0, "", ""});
    CHECK(info.releaseNotes.find("Exciting new DJ features") != std::string::npos);
    CHECK(info.downloadUrl == "https://github.com/zyron-dj/zyron/releases/tag/v0.2.0");
    CHECK(mockTransport->getCallCount == 1);
  }

  SECTION("Up to date detected when remote version matches or is older") {
    mockTransport->responseToReturn.statusCode = 200;
    mockTransport->responseToReturn.body = R"({
      "tag_name": "v0.1.0",
      "body": "Initial release",
      "html_url": "https://github.com/zyron-dj/zyron/releases/tag/v0.1.0"
    })";

    const auto info = checker.checkUpdate(false);
    CHECK(info.status == core::UpdateCheckStatus::UpToDate);
    CHECK(info.latestVersion == currentVer);
  }

  SECTION("Network error handled gracefully without throw") {
    mockTransport->responseToReturn.statusCode = 404;
    mockTransport->responseToReturn.errorMessage = "Not Found";

    const auto info = checker.checkUpdate(false);
    CHECK(info.status == core::UpdateCheckStatus::NetworkError);
    CHECK_FALSE(info.errorMessage.empty());
  }

  SECTION("Malformed JSON returns ParseError") {
    mockTransport->responseToReturn.statusCode = 200;
    mockTransport->responseToReturn.body = "Not a json payload";

    const auto info = checker.checkUpdate(false);
    CHECK(info.status == core::UpdateCheckStatus::ParseError);
  }
}
