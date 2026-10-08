// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/MainWindow.hpp"

#include <memory>

#include "BrandingData.h"
#include "UI/Theme.hpp"

namespace zyron::ui {

MainWindow::MainWindow(const juce::String& name, core::CommandBus& bus, const core::AudioEngineStatsSource& stats,
                       std::shared_ptr<core::ILibrarySource> library, core::ILiveEngineSource* live,
                       const core::IDeckLoadSource* loads, core::IAutomixControl* automix)
    : DocumentWindow(name, Theme::dark().background, DocumentWindow::allButtons),
      bus_(bus),
      stats_(stats),
      library_(std::move(library)),
      live_(live),
      loads_(loads),
      automix_(automix) {
  setUsingNativeTitleBar(true);
  setIcon(juce::ImageCache::getFromMemory(zyron_branding::zyronicon_png, zyron_branding::zyronicon_pngSize));
  setResizable(true, true);
  buildContent(false);
  centreWithSize(getWidth(), getHeight());
  setVisible(true);
}

void MainWindow::buildContent(bool settingsOpen) {
  auto content = std::make_unique<MainComponent>(Theme::dark(), bus_, stats_, library_, live_, loads_, automix_);
  content_ = content.get();
  content_->onLanguageChanged = [safe = juce::Component::SafePointer<MainWindow>(this)] {
    // Rebuild after the click handler has returned: the handler lives inside the component being replaced.
    juce::MessageManager::callAsync([safe] {
      if (safe != nullptr) {
        safe->buildContent(true);
      }
    });
  };
  const auto bounds = getBounds();
  setContentOwned(content.release(), true);  // JUCE takes ownership of the content component (its documented idiom)
  if (!bounds.isEmpty()) {
    setBounds(bounds);
  }
  if (hardwareReport_.has_value()) {
    content_->showHardwareReport(*hardwareReport_);
  }
  if (settingsOpen) {
    content_->setSettingsVisible(true);
  }
}

void MainWindow::showHardwareReport(const core::HardwareReport& report) {
  hardwareReport_ = report;
  content_->showHardwareReport(report);
}

void MainWindow::closeButtonPressed() {
  juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

}  // namespace zyron::ui
