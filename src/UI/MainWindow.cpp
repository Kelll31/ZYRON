// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/MainWindow.hpp"

#include <memory>

#include "UI/Theme.hpp"

namespace zyron::ui {

MainWindow::MainWindow(const juce::String& name, core::CommandBus& bus, const core::AudioEngineStatsSource& stats)
    : DocumentWindow(name, Theme::dark().background, DocumentWindow::allButtons) {
  setUsingNativeTitleBar(true);
  setResizable(true, true);
  auto content = std::make_unique<MainComponent>(Theme::dark(), bus, stats);
  content_ = content.get();
  setContentOwned(content.release(), true);  // JUCE takes ownership of the content component (its documented idiom)
  centreWithSize(getWidth(), getHeight());
  setVisible(true);
}

void MainWindow::showHardwareReport(const core::HardwareReport& report) {
  content_->showHardwareReport(report);
}

void MainWindow::closeButtonPressed() {
  juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

}  // namespace zyron::ui
