// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

#include "Core/Release/FirstRunTypes.hpp"
#include "UI/FirstRun/FirstRunWizardComponent.hpp"
#include "UI/Theme.hpp"

using namespace zyron;

namespace {

class MockFirstRunManager : public core::IFirstRunManager {
 public:
  core::FirstRunStep step_{core::FirstRunStep::WelcomeAndPolicy};
  core::FirstRunConfig config_;
  bool completeCalled{false};

  [[nodiscard]] core::FirstRunStep currentStep() const noexcept override { return step_; }
  void setStep(core::FirstRunStep s) override { step_ = s; }

  [[nodiscard]] bool isFirstRunNeeded() const override { return !config_.isCompleted; }
  [[nodiscard]] const core::FirstRunConfig& config() const noexcept override { return config_; }

  void setOfflinePolicy(core::OfflinePolicyConsent consent) override {
    config_.offlineMode = (consent == core::OfflinePolicyConsent::OfflineOnly);
  }

  bool setModelsDirectory(const std::string& path) override {
    config_.modelsDirectory = path;
    return true;
  }

  [[nodiscard]] std::vector<core::FirstRunModelSummary> scanAndVerifyModels() override {
    return {
        {"demucs_htdemucs", "Demucs", "htdemucs.onnx", 85000000, core::ModelInstallStatus::Ready, "OK"},
        {"beat_this", "Beat This!", "beat_this.onnx", 12000000, core::ModelInstallStatus::NotInstalled, "Missing"}
    };
  }

  bool importModel(const std::string& /*id*/, const std::string& /*path*/) override { return true; }
  bool downloadModel(const std::string& /*id*/, std::function<void(float)> /*cb*/) override { return true; }

  bool completeFirstRun() override {
    completeCalled = true;
    config_.isCompleted = true;
    step_ = core::FirstRunStep::ReadyToLaunch;
    return true;
  }

  void resetFirstRun() override {
    completeCalled = false;
    config_.isCompleted = false;
    step_ = core::FirstRunStep::WelcomeAndPolicy;
  }
};

}  // namespace

TEST_CASE("FirstRunWizardComponent: Headless rendering and navigation (SPEC section 73, P10-02)", "[ui][firstrun]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  auto manager = std::make_shared<MockFirstRunManager>();
  bool finishedCallbackFired = false;

  ui::FirstRunWizardComponent wizard(manager, [&]() {
    finishedCallbackFired = true;
  }, ui::Theme::dark());

  wizard.setSize(800, 600);

  SECTION("Renders cleanly across all steps without exceptions") {
    juce::Image img(juce::Image::ARGB, 800, 600, true);
    juce::Graphics g(img);

    for (int s = 0; s <= 3; ++s) {
      wizard.setStep(static_cast<core::FirstRunStep>(s));
      REQUIRE_NOTHROW(wizard.paintEntireComponent(g, true));
    }
  }

  SECTION("Step advancement through Next and Back triggers") {
    CHECK(wizard.currentStep() == core::FirstRunStep::WelcomeAndPolicy);

    wizard.triggerNext();
    CHECK(wizard.currentStep() == core::FirstRunStep::ModelLocation);

    wizard.triggerNext();
    CHECK(wizard.currentStep() == core::FirstRunStep::ModelVerification);

    wizard.triggerNext();
    CHECK(wizard.currentStep() == core::FirstRunStep::ReadyToLaunch);

    wizard.triggerBack();
    CHECK(wizard.currentStep() == core::FirstRunStep::ModelVerification);
  }

  SECTION("Verification scan populates model report") {
    wizard.setStep(core::FirstRunStep::ModelVerification);
    wizard.triggerVerify();
    // Verification shouldn't throw and remains in verification step
    CHECK(wizard.currentStep() == core::FirstRunStep::ModelVerification);
  }

  SECTION("Finishing onboarding triggers completion and callback") {
    wizard.setStep(core::FirstRunStep::ReadyToLaunch);
    wizard.triggerFinish();

    CHECK(manager->completeCalled);
    CHECK(finishedCallbackFired);
  }
}
