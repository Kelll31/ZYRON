// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Core/Release/UpdateTypes.hpp"

namespace zyron::platform {

/// Production software update checker respecting the offline-first network policy (SPEC sections 72, 75, ROADMAP P10-03).
class UpdateChecker : public core::IUpdateChecker {
 public:
  explicit UpdateChecker(core::SemVer currentVersion,
                         std::shared_ptr<core::IHttpTransport> transport = nullptr,
                         std::string defaultFeedUrl = "https://api.github.com/repos/zyron-dj/zyron/releases/latest");
  ~UpdateChecker() override = default;

  [[nodiscard]] const core::SemVer& currentVersion() const noexcept { return currentVersion_; }
  [[nodiscard]] const std::string& defaultFeedUrl() const noexcept { return defaultFeedUrl_; }

  [[nodiscard]] core::UpdateInfo checkUpdate(bool offlineMode, const std::string& feedUrl = "") override;

  void checkUpdateAsync(bool offlineMode,
                        std::function<void(core::UpdateInfo)> callback,
                        const std::string& feedUrl = "") override;

  /// Helper to parse release JSON responses (supports both GitHub Releases API format and ZYRON manifest format).
  [[nodiscard]] static core::UpdateInfo parseReleaseJson(const std::string& jsonString,
                                                         const core::SemVer& currentVersion);

 private:
  core::SemVer currentVersion_;
  std::shared_ptr<core::IHttpTransport> transport_;
  std::string defaultFeedUrl_;
};

}  // namespace zyron::platform
