// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Core/Release/FirstRunTypes.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Onboarding wizard for initial setup, offline policy selection, and AI model verification (SPEC section 73, ROADMAP P10-02).
class FirstRunWizardComponent : public juce::Component, public juce::Button::Listener {
 public:
  explicit FirstRunWizardComponent(std::shared_ptr<core::IFirstRunManager> manager,
                                   std::function<void()> onFinished = nullptr,
                                   Theme theme = Theme::dark());
  ~FirstRunWizardComponent() override;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void buttonClicked(juce::Button* button) override;

  [[nodiscard]] core::FirstRunStep currentStep() const noexcept;
  void setStep(core::FirstRunStep step);

  void refreshModelsView();

  // Programmatic triggers for unit testing and headless validation
  void triggerNext();
  void triggerBack();
  void triggerVerify();
  void triggerFinish();

 private:
  void updateVisibility();

  std::shared_ptr<core::IFirstRunManager> manager_;
  std::function<void()> onFinished_;
  Theme theme_;

  juce::Label headerLabel_;
  juce::Label subtitleLabel_;

  // Step 1: Welcome & Offline Policy
  juce::Label policyInfoLabel_;
  juce::ToggleButton offlineOnlyButton_{"Air-gapped / Offline Only (Zero network usage)"};
  juce::ToggleButton allowNetworkButton_{"Allow Network (Version check & model downloads only)"};

  // Step 2: Model Storage
  juce::Label dirPromptLabel_;
  juce::Label dirCurrentLabel_;
  juce::TextButton useDefaultDirButton_{"Use Default (./models)"};
  juce::TextButton useCustomDirButton_{"Set Custom Directory..."};

  // Step 3: Model Verification
  juce::Label modelsHeaderLabel_;
  juce::TextButton verifyModelsButton_{"Scan & Verify All Models"};
  juce::TextButton downloadModelsButton_{"Download Missing (Online)"};
  juce::Label modelStatusReportLabel_;

  // Step 4: Ready
  juce::Label completionMessageLabel_;
  juce::TextButton launchAppButton_{"Launch ZYRON DJ"};

  // Navigation Buttons
  juce::TextButton backButton_{"Back"};
  juce::TextButton nextButton_{"Next"};
};

}  // namespace zyron::ui
