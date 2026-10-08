// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "Core/Release/FirstRunTypes.hpp"
#include "Core/Release/UpdateTypes.hpp"
#include "Platform/Release/FirstRunManager.hpp"

using namespace zyron;

namespace {

class MockDownloadTransport : public core::IHttpTransport {
 public:
  int downloadCalls{0};

  HttpResponse get(const std::string& /*url*/, int /*timeoutMs*/) override {
    return {200, "{}", ""};
  }

  bool downloadFile(const std::string& /*url*/,
                    const std::string& destinationPath,
                    std::function<void(float progress)> onProgress,
                    int /*timeoutMs*/) override {
    ++downloadCalls;
    if (onProgress) onProgress(1.0f);
    // Touch destination file
    std::ofstream f(destinationPath);
    f << "mock onnx model bytes";
    return true;
  }
};

}  // namespace

TEST_CASE("FirstRunManager: Onboarding lifecycle, offline policy, and models (SPEC section 73, P10-02)", "[release][firstrun]") {
  const std::filesystem::path testDir = std::filesystem::temp_directory_path() / "zyron_first_run_test";
  std::filesystem::create_directories(testDir);
  const std::string configPath = (testDir / "first_run_config.json").string();
  const std::string modelsDir = (testDir / "models").string();

  // Clean previous run if existing
  std::error_code ec;
  std::filesystem::remove_all(testDir, ec);
  std::filesystem::create_directories(testDir, ec);

  auto transport = std::make_shared<MockDownloadTransport>();
  platform::FirstRunManager manager(configPath, nullptr, transport);

  SECTION("Initial state requires onboarding") {
    CHECK(manager.isFirstRunNeeded());
    CHECK(manager.currentStep() == core::FirstRunStep::WelcomeAndPolicy);
    CHECK(manager.config().offlineMode);  // Default is offline-first!
  }

  SECTION("Step transitions and offline policy configuration") {
    manager.setOfflinePolicy(core::OfflinePolicyConsent::OfflineOnly);
    CHECK(manager.config().offlineMode);

    manager.setStep(core::FirstRunStep::ModelLocation);
    CHECK(manager.currentStep() == core::FirstRunStep::ModelLocation);

    CHECK(manager.setModelsDirectory(modelsDir));
    CHECK(manager.config().modelsDirectory == modelsDir);
    CHECK(std::filesystem::exists(modelsDir));
  }

  SECTION("Model verification and file importing") {
    manager.setModelsDirectory(modelsDir);
    auto summaries = manager.scanAndVerifyModels();
    REQUIRE_FALSE(summaries.empty());

    // Initially all missing
    for (const auto& s : summaries) {
      CHECK(s.status == core::ModelInstallStatus::NotInstalled);
    }

    // Create a dummy source model file
    const std::filesystem::path dummySource = testDir / "beat_this.onnx";
    {
      std::ofstream f(dummySource);
      f << "dummy model data";
    }

    // Import it
    CHECK(manager.importModel("beat_this", dummySource.string()));

    // Verify it is now recognized
    summaries = manager.scanAndVerifyModels();
    bool foundReady = false;
    for (const auto& s : summaries) {
      if (s.modelId == "beat_this") {
        CHECK(s.status == core::ModelInstallStatus::Ready);
        foundReady = true;
      }
    }
    CHECK(foundReady);
  }

  SECTION("Downloading models respects offline policy") {
    manager.setModelsDirectory(modelsDir);

    // 1. In offline mode, download MUST fail immediately
    manager.setOfflinePolicy(core::OfflinePolicyConsent::OfflineOnly);
    CHECK_FALSE(manager.downloadModel("demucs_htdemucs"));
    CHECK(transport->downloadCalls == 0);

    // 2. In network-allowed mode, download succeeds
    manager.setOfflinePolicy(core::OfflinePolicyConsent::AllowNetworkChecks);
    CHECK(manager.downloadModel("demucs_htdemucs"));
    CHECK(transport->downloadCalls == 1);
  }

  SECTION("Completing onboarding persists config and stops prompting") {
    CHECK(manager.completeFirstRun());
    CHECK(manager.currentStep() == core::FirstRunStep::ReadyToLaunch);
    CHECK_FALSE(manager.isFirstRunNeeded());
    CHECK(manager.config().isCompleted);
    CHECK_FALSE(manager.config().completedTimestamp.empty());
    CHECK(std::filesystem::exists(configPath));

    // Reload from new instance to prove persistence
    platform::FirstRunManager reloaded(configPath);
    CHECK_FALSE(reloaded.isFirstRunNeeded());
    CHECK(reloaded.config().isCompleted);
    CHECK(reloaded.config().modelsDirectory == manager.config().modelsDirectory);

    // Resetting works
    reloaded.resetFirstRun();
    CHECK(reloaded.isFirstRunNeeded());
    CHECK_FALSE(reloaded.config().isCompleted);
  }

  // Teardown
  std::filesystem::remove_all(testDir, ec);
}
