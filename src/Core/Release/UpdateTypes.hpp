// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace zyron::core {

/// Semantic Version representation conforming to SemVer 2.0.0.
struct SemVer {
  int major{0};
  int minor{0};
  int patch{0};
  std::string prerelease;
  std::string build;

  [[nodiscard]] static std::optional<SemVer> parse(std::string_view versionStr) noexcept {
    if (versionStr.empty()) return std::nullopt;
    // Strip optional leading 'v' or 'V'
    if (versionStr.front() == 'v' || versionStr.front() == 'V') {
      versionStr.remove_prefix(1);
    }

    std::string s(versionStr);
    std::string buildPart;
    const auto plusPos = s.find('+');
    if (plusPos != std::string::npos) {
      buildPart = s.substr(plusPos + 1);
      s = s.substr(0, plusPos);
    }

    std::string prereleasePart;
    const auto hyphenPos = s.find('-');
    if (hyphenPos != std::string::npos) {
      prereleasePart = s.substr(hyphenPos + 1);
      s = s.substr(0, hyphenPos);
    }

    int maj = 0;
    int min = 0;
    int pat = 0;
    std::stringstream ss(s);
    char dot1 = '\0';
    char dot2 = '\0';
    if (!(ss >> maj >> dot1 >> min >> dot2 >> pat) || dot1 != '.' || dot2 != '.') {
      return std::nullopt;
    }
    if (maj < 0 || min < 0 || pat < 0) return std::nullopt;

    SemVer res;
    res.major = maj;
    res.minor = min;
    res.patch = pat;
    res.prerelease = std::move(prereleasePart);
    res.build = std::move(buildPart);
    return res;
  }

  [[nodiscard]] std::string toString() const {
    std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) {
      s += "-" + prerelease;
    }
    if (!build.empty()) {
      s += "+" + build;
    }
    return s;
  }

  [[nodiscard]] bool operator==(const SemVer& other) const noexcept {
    return major == other.major && minor == other.minor && patch == other.patch && prerelease == other.prerelease;
  }

  [[nodiscard]] bool operator!=(const SemVer& other) const noexcept {
    return !(*this == other);
  }

  [[nodiscard]] bool operator<(const SemVer& other) const noexcept {
    if (major != other.major) return major < other.major;
    if (minor != other.minor) return minor < other.minor;
    if (patch != other.patch) return patch < other.patch;

    // SemVer 2.0.0 rule: a normal version has higher precedence than a prerelease version
    if (prerelease.empty() && !other.prerelease.empty()) return false;
    if (!prerelease.empty() && other.prerelease.empty()) return true;
    return prerelease < other.prerelease;
  }

  [[nodiscard]] bool operator<=(const SemVer& other) const noexcept {
    return (*this < other) || (*this == other);
  }

  [[nodiscard]] bool operator>(const SemVer& other) const noexcept {
    return !(*this <= other);
  }

  [[nodiscard]] bool operator>=(const SemVer& other) const noexcept {
    return !(*this < other);
  }
};

/// Lifecycle status for software update check (SPEC section 75).
enum class UpdateCheckStatus : std::uint8_t {
  UpToDate = 0,
  UpdateAvailable,
  OfflineModeBlocked,
  NetworkError,
  ParseError
};

[[nodiscard]] constexpr std::string_view updateCheckStatusName(UpdateCheckStatus s) noexcept {
  switch (s) {
    case UpdateCheckStatus::UpToDate:
      return "Up To Date";
    case UpdateCheckStatus::UpdateAvailable:
      return "Update Available";
    case UpdateCheckStatus::OfflineModeBlocked:
      return "Offline Mode (Update Check Blocked)";
    case UpdateCheckStatus::NetworkError:
      return "Network Error";
    case UpdateCheckStatus::ParseError:
      return "Parse Error";
  }
  return "Unknown";
}

/// Release track channel.
enum class ReleaseChannel : std::uint8_t {
  Stable = 0,
  Beta,
  Nightly
};

/// Detail container returned by the update check service.
struct UpdateInfo {
  SemVer currentVersion;
  SemVer latestVersion;
  ReleaseChannel channel{ReleaseChannel::Stable};
  UpdateCheckStatus status{UpdateCheckStatus::UpToDate};
  std::string releaseNotes;
  std::string downloadUrl;
  std::string publishedDate;
  bool isMandatory{false};
  std::string errorMessage;
};

/// Abstraction for HTTP transport so network interactions are mockable and isolated (SPEC section 75).
class IHttpTransport {
 public:
  virtual ~IHttpTransport() = default;

  struct HttpResponse {
    int statusCode{0};
    std::string body;
    std::string errorMessage;

    [[nodiscard]] bool isSuccess() const noexcept {
      return statusCode >= 200 && statusCode < 300;
    }
  };

  [[nodiscard]] virtual HttpResponse get(const std::string& url, int timeoutMs = 5000) = 0;

  virtual bool downloadFile(const std::string& url,
                            const std::string& destinationPath,
                            std::function<void(float progress)> onProgress = nullptr,
                            int timeoutMs = 30000) = 0;
};

/// Update check service interface (SPEC section 75).
class IUpdateChecker {
 public:
  virtual ~IUpdateChecker() = default;

  [[nodiscard]] virtual UpdateInfo checkUpdate(bool offlineMode, const std::string& feedUrl = "") = 0;

  virtual void checkUpdateAsync(bool offlineMode,
                                std::function<void(UpdateInfo)> callback,
                                const std::string& feedUrl = "") = 0;
};

}  // namespace zyron::core
