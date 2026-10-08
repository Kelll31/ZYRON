// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/FirstRun/FirstRunWizardComponent.hpp"
#include "UI/Localization.hpp"

#include <sstream>

namespace zyron::ui {

FirstRunWizardComponent::FirstRunWizardComponent(std::shared_ptr<core::IFirstRunManager> manager,
                                                 std::function<void()> onFinished,
                                                 Theme theme)
    : manager_(std::move(manager)),
      onFinished_(std::move(onFinished)),
      theme_(theme) {
  setOpaque(true);

  // Headers
  headerLabel_.setText(TRANS("ZYRON - Setup Wizard"), juce::dontSendNotification);
  headerLabel_.setFont(juce::FontOptions{22.0F, juce::Font::bold});
  headerLabel_.setColour(juce::Label::textColourId, theme_.text);
  addAndMakeVisible(headerLabel_);

  subtitleLabel_.setFont(juce::FontOptions{14.0F});
  subtitleLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(subtitleLabel_);

  // Step 1: Policy
  policyInfoLabel_.setText(
      juce::String(TRANS("ZYRON is designed offline-first (SPEC %s). Playback, mixing, stems, and analysis work "
                         "completely without an internet connection.\nChoose your network policy below:"))
          .replace("%s", juce::String::fromUTF8("\xc2\xa7""75")),
      juce::dontSendNotification);
  policyInfoLabel_.setFont(juce::FontOptions{13.0F});
  policyInfoLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(policyInfoLabel_);

  offlineOnlyButton_.setRadioGroupId(1001);
  allowNetworkButton_.setRadioGroupId(1001);
  offlineOnlyButton_.setToggleState(true, juce::dontSendNotification);
  offlineOnlyButton_.addListener(this);
  allowNetworkButton_.addListener(this);
  addAndMakeVisible(offlineOnlyButton_);
  addAndMakeVisible(allowNetworkButton_);

  // Step 2: Directory
  dirPromptLabel_.setText(TRANS("Select where AI model weights (.onnx) are stored on disk:"),
                          juce::dontSendNotification);
  dirPromptLabel_.setFont(juce::FontOptions{13.0F});
  dirPromptLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(dirPromptLabel_);

  dirCurrentLabel_.setFont(juce::FontOptions{13.0F});
  dirCurrentLabel_.setColour(juce::Label::textColourId, theme_.text);
  if (manager_) {
    dirCurrentLabel_.setText(juce::String(TRANS("Current path:")) + " " + juce::String(manager_->config().modelsDirectory),
                             juce::dontSendNotification);
  }
  addAndMakeVisible(dirCurrentLabel_);

  useDefaultDirButton_.addListener(this);
  useCustomDirButton_.addListener(this);
  addAndMakeVisible(useDefaultDirButton_);
  addAndMakeVisible(useCustomDirButton_);

  // Step 3: Models
  modelsHeaderLabel_.setText(juce::String(TRANS("Model Verification Status")) + juce::String::fromUTF8(" (SPEC \xc2\xa7""73, \xc2\xa7""74):"), juce::dontSendNotification);
  modelsHeaderLabel_.setFont(juce::FontOptions{15.0F, juce::Font::bold});
  modelsHeaderLabel_.setColour(juce::Label::textColourId, theme_.text);
  addAndMakeVisible(modelsHeaderLabel_);

  verifyModelsButton_.addListener(this);
  downloadModelsButton_.addListener(this);
  addAndMakeVisible(verifyModelsButton_);
  addAndMakeVisible(downloadModelsButton_);

  modelStatusReportLabel_.setFont(juce::FontOptions{12.0F});
  modelStatusReportLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  addAndMakeVisible(modelStatusReportLabel_);

  // Step 4: Completion
  completionMessageLabel_.setText(
      TRANS("All set! Your preferences and model configurations have been saved.\n"
            "Click Launch to start mixing tracks."),
      juce::dontSendNotification);
  completionMessageLabel_.setFont(juce::FontOptions{16.0F, juce::Font::bold});
  completionMessageLabel_.setColour(juce::Label::textColourId, theme_.text);
  addAndMakeVisible(completionMessageLabel_);

  launchAppButton_.addListener(this);
  addAndMakeVisible(launchAppButton_);

  // Nav buttons
  backButton_.addListener(this);
  nextButton_.addListener(this);
  addAndMakeVisible(backButton_);
  addAndMakeVisible(nextButton_);

  updateVisibility();
}

FirstRunWizardComponent::~FirstRunWizardComponent() {
  offlineOnlyButton_.removeListener(this);
  allowNetworkButton_.removeListener(this);
  useDefaultDirButton_.removeListener(this);
  useCustomDirButton_.removeListener(this);
  verifyModelsButton_.removeListener(this);
  downloadModelsButton_.removeListener(this);
  launchAppButton_.removeListener(this);
  backButton_.removeListener(this);
  nextButton_.removeListener(this);
}

void FirstRunWizardComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);

  // Decorative border around wizard container
  g.setColour(theme_.panel);
  g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(16.0f), 8.0f);

  g.setColour(theme_.accent);
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(16.0f), 8.0f, 1.0f);
}

void FirstRunWizardComponent::resized() {
  const auto bounds = getLocalBounds().reduced(32);
  auto area = bounds;

  headerLabel_.setBounds(area.removeFromTop(32));
  subtitleLabel_.setBounds(area.removeFromTop(24));
  area.removeFromTop(16);

  // Bottom navigation row
  auto navArea = area.removeFromBottom(40);
  backButton_.setBounds(navArea.removeFromLeft(100));
  nextButton_.setBounds(navArea.removeFromRight(120));

  // Step 1 layout
  policyInfoLabel_.setBounds(area.removeFromTop(48));
  offlineOnlyButton_.setBounds(area.removeFromTop(32));
  allowNetworkButton_.setBounds(area.removeFromTop(32));

  // Step 2 layout
  dirPromptLabel_.setBounds(area.removeFromTop(32));
  dirCurrentLabel_.setBounds(area.removeFromTop(28));
  auto dirBtnArea = area.removeFromTop(36);
  useDefaultDirButton_.setBounds(dirBtnArea.removeFromLeft(160));
  dirBtnArea.removeFromLeft(12);
  useCustomDirButton_.setBounds(dirBtnArea.removeFromLeft(180));

  // Step 3 layout
  modelsHeaderLabel_.setBounds(area.removeFromTop(28));
  auto modelBtnArea = area.removeFromTop(36);
  verifyModelsButton_.setBounds(modelBtnArea.removeFromLeft(180));
  modelBtnArea.removeFromLeft(12);
  downloadModelsButton_.setBounds(modelBtnArea.removeFromLeft(180));
  area.removeFromTop(8);
  modelStatusReportLabel_.setBounds(area.removeFromTop(120));

  // Step 4 layout
  completionMessageLabel_.setBounds(area.removeFromTop(60));
  launchAppButton_.setBounds(area.removeFromTop(44).reduced(60, 0));
}

core::FirstRunStep FirstRunWizardComponent::currentStep() const noexcept {
  if (manager_) {
    return manager_->currentStep();
  }
  return core::FirstRunStep::WelcomeAndPolicy;
}

void FirstRunWizardComponent::setStep(core::FirstRunStep step) {
  if (manager_) {
    manager_->setStep(step);
  }
  updateVisibility();
  repaint();
}

void FirstRunWizardComponent::updateVisibility() {
  const auto step = currentStep();

  subtitleLabel_.setText(
      juce::String(TRANS("Step %n of 4: %s"))
          .replace("%n", juce::String(static_cast<int>(step) + 1))
          .replace("%s", i18n::translateMessage(std::string(core::firstRunStepName(step)))),
      juce::dontSendNotification);

  const bool isStep1 = (step == core::FirstRunStep::WelcomeAndPolicy);
  const bool isStep2 = (step == core::FirstRunStep::ModelLocation);
  const bool isStep3 = (step == core::FirstRunStep::ModelVerification);
  const bool isStep4 = (step == core::FirstRunStep::ReadyToLaunch);

  policyInfoLabel_.setVisible(isStep1);
  offlineOnlyButton_.setVisible(isStep1);
  allowNetworkButton_.setVisible(isStep1);

  dirPromptLabel_.setVisible(isStep2);
  dirCurrentLabel_.setVisible(isStep2);
  useDefaultDirButton_.setVisible(isStep2);
  useCustomDirButton_.setVisible(isStep2);

  modelsHeaderLabel_.setVisible(isStep3);
  verifyModelsButton_.setVisible(isStep3);
  downloadModelsButton_.setVisible(isStep3);
  modelStatusReportLabel_.setVisible(isStep3);

  completionMessageLabel_.setVisible(isStep4);
  launchAppButton_.setVisible(isStep4);

  backButton_.setVisible(!isStep1 && !isStep4);
  nextButton_.setVisible(!isStep4);

  if (manager_) {
    downloadModelsButton_.setEnabled(!manager_->config().offlineMode);
  }
}

void FirstRunWizardComponent::refreshModelsView() {
  if (!manager_) return;

  const auto summaries = manager_->scanAndVerifyModels();
  std::stringstream ss;
  for (const auto& s : summaries) {
    ss << "[" << i18n::translateMessage(core::modelInstallStatusName(s.status)).toStdString() << "] "
       << s.name << " (" << s.filename
       << "): " << i18n::translateMessage(s.verificationMessage).toStdString() << "\n";
  }
  modelStatusReportLabel_.setText(ss.str(), juce::dontSendNotification);
}

void FirstRunWizardComponent::buttonClicked(juce::Button* button) {
  if (button == &offlineOnlyButton_ && manager_) {
    manager_->setOfflinePolicy(core::OfflinePolicyConsent::OfflineOnly);
    updateVisibility();
  } else if (button == &allowNetworkButton_ && manager_) {
    manager_->setOfflinePolicy(core::OfflinePolicyConsent::AllowNetworkChecks);
    updateVisibility();
  } else if (button == &useDefaultDirButton_ && manager_) {
    manager_->setModelsDirectory("models");
    dirCurrentLabel_.setText(TRANS("Current path: models"), juce::dontSendNotification);
  } else if (button == &useCustomDirButton_ && manager_) {
    manager_->setModelsDirectory("custom_models");
    dirCurrentLabel_.setText(TRANS("Current path: custom_models"), juce::dontSendNotification);
  } else if (button == &verifyModelsButton_) {
    refreshModelsView();
  } else if (button == &downloadModelsButton_ && manager_) {
    manager_->downloadModel("demucs_htdemucs");
    refreshModelsView();
  } else if (button == &nextButton_) {
    triggerNext();
  } else if (button == &backButton_) {
    triggerBack();
  } else if (button == &launchAppButton_) {
    triggerFinish();
  }
}

void FirstRunWizardComponent::triggerNext() {
  const auto step = currentStep();
  switch (step) {
    case core::FirstRunStep::WelcomeAndPolicy:
      setStep(core::FirstRunStep::ModelLocation);
      break;
    case core::FirstRunStep::ModelLocation:
      setStep(core::FirstRunStep::ModelVerification);
      refreshModelsView();
      break;
    case core::FirstRunStep::ModelVerification:
      setStep(core::FirstRunStep::ReadyToLaunch);
      break;
    case core::FirstRunStep::ReadyToLaunch:
      triggerFinish();
      break;
  }
}

void FirstRunWizardComponent::triggerBack() {
  const auto step = currentStep();
  switch (step) {
    case core::FirstRunStep::ModelLocation:
      setStep(core::FirstRunStep::WelcomeAndPolicy);
      break;
    case core::FirstRunStep::ModelVerification:
      setStep(core::FirstRunStep::ModelLocation);
      break;
    case core::FirstRunStep::ReadyToLaunch:
      setStep(core::FirstRunStep::ModelVerification);
      break;
    case core::FirstRunStep::WelcomeAndPolicy:
      break;
  }
}

void FirstRunWizardComponent::triggerVerify() {
  refreshModelsView();
}

void FirstRunWizardComponent::triggerFinish() {
  if (manager_) {
    manager_->completeFirstRun();
  }
  if (onFinished_) {
    onFinished_();
  }
}

}  // namespace zyron::ui
