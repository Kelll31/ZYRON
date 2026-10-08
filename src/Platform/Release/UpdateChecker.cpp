// SPDX-License-Identifier: AGPL-3.0-only
#include "Platform/Release/UpdateChecker.hpp"

#include <regex>
#include <thread>

namespace zyron::platform {

namespace {

std::string extractJsonField(const std::string& json, const std::string& fieldName) {
  const std::string pattern = "\"" + fieldName + "\"\\s*:\\s*\"([^\"]*)\"";
  const std::regex strRegex(pattern);
  std::smatch match;
  if (std::regex_search(json, match, strRegex) && match.size() > 1) {
    return match[1].str();
  }
  return "";
}

bool extractJsonBool(const std::string& json, const std::string& fieldName, bool defaultValue = false) {
  const std::string pattern = "\"" + fieldName + "\"\\s*:\\s*(true|false)";
  const std::regex boolRegex(pattern);
  std::smatch match;
  if (std::regex_search(json, match, boolRegex) && match.size() > 1) {
    return match[1].str() == "true";
  }
  return defaultValue;
}

}  // namespace

UpdateChecker::UpdateChecker(core::SemVer currentVersion,
                             std::shared_ptr<core::IHttpTransport> transport,
                             std::string defaultFeedUrl)
    : currentVersion_(std::move(currentVersion)),
      transport_(std::move(transport)),
      defaultFeedUrl_(std::move(defaultFeedUrl)) {}

core::UpdateInfo UpdateChecker::parseReleaseJson(const std::string& jsonString,
                                                 const core::SemVer& currentVersion) {
  core::UpdateInfo info;
  info.currentVersion = currentVersion;

  if (jsonString.empty()) {
    info.status = core::UpdateCheckStatus::ParseError;
    info.errorMessage = "Empty JSON payload";
    return info;
  }

  // Check version field: GitHub "tag_name" or generic "version"
  std::string verStr = extractJsonField(jsonString, "tag_name");
  if (verStr.empty()) {
    verStr = extractJsonField(jsonString, "version");
  }

  if (verStr.empty()) {
    info.status = core::UpdateCheckStatus::ParseError;
    info.errorMessage = "Missing version or tag_name field in release payload";
    return info;
  }

  auto parsedVer = core::SemVer::parse(verStr);
  if (!parsedVer.has_value()) {
    info.status = core::UpdateCheckStatus::ParseError;
    info.errorMessage = "Invalid semantic version string: " + verStr;
    return info;
  }

  info.latestVersion = *parsedVer;

  // Release notes: GitHub "body" or generic "notes"
  std::string notes = extractJsonField(jsonString, "body");
  if (notes.empty()) {
    notes = extractJsonField(jsonString, "notes");
  }
  info.releaseNotes = std::move(notes);

  // Download URL: GitHub "html_url" or generic "download_url"
  std::string url = extractJsonField(jsonString, "download_url");
  if (url.empty()) {
    url = extractJsonField(jsonString, "html_url");
  }
  info.downloadUrl = std::move(url);

  info.publishedDate = extractJsonField(jsonString, "published_at");
  info.isMandatory = extractJsonBool(jsonString, "mandatory", false);

  const bool isPrerelease = extractJsonBool(jsonString, "prerelease", false);
  info.channel = isPrerelease ? core::ReleaseChannel::Beta : core::ReleaseChannel::Stable;

  if (info.latestVersion > currentVersion) {
    info.status = core::UpdateCheckStatus::UpdateAvailable;
  } else {
    info.status = core::UpdateCheckStatus::UpToDate;
  }

  return info;
}

core::UpdateInfo UpdateChecker::checkUpdate(bool offlineMode, const std::string& feedUrl) {
  core::UpdateInfo info;
  info.currentVersion = currentVersion_;
  info.latestVersion = currentVersion_;

  // SPEC §75: Offline-first policy enforcement.
  if (offlineMode) {
    info.status = core::UpdateCheckStatus::OfflineModeBlocked;
    info.errorMessage = "Offline mode active: all network update queries are strictly blocked";
    return info;
  }

  if (!transport_) {
    info.status = core::UpdateCheckStatus::NetworkError;
    info.errorMessage = "HTTP transport is unconfigured";
    return info;
  }

  const std::string urlToQuery = feedUrl.empty() ? defaultFeedUrl_ : feedUrl;
  const auto response = transport_->get(urlToQuery);

  if (!response.isSuccess()) {
    info.status = core::UpdateCheckStatus::NetworkError;
    info.errorMessage = "HTTP request failed (status " + std::to_string(response.statusCode) + "): " +
                        response.errorMessage;
    return info;
  }

  return parseReleaseJson(response.body, currentVersion_);
}

void UpdateChecker::checkUpdateAsync(bool offlineMode,
                                     std::function<void(core::UpdateInfo)> callback,
                                     const std::string& feedUrl) {
  if (!callback) return;

  std::thread([this, offlineMode, callback = std::move(callback), feedUrl]() {
    const auto result = checkUpdate(offlineMode, feedUrl);
    callback(result);
  }).detach();
}

}  // namespace zyron::platform
