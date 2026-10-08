// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>

#include "Core/AI/AutonomousDjTypes.hpp"
#include "Core/Audio/EngineView.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/System/EngineStats.hpp"
#include "Core/System/HardwareInfo.hpp"
#include "UI/MainComponent.hpp"

namespace zyron::ui {

/// The top-level window. The application's composition root creates it and feeds it data; it knows nothing about the
/// engine beyond the read-only stats source and the CommandBus.
class MainWindow final : public juce::DocumentWindow {
 public:
  MainWindow(const juce::String& name, core::CommandBus& bus, const core::AudioEngineStatsSource& stats,
             std::shared_ptr<core::ILibrarySource> library = nullptr, core::ILiveEngineSource* live = nullptr,
             const core::IDeckLoadSource* loads = nullptr, core::IAutomixControl* automix = nullptr);

  void showHardwareReport(const core::HardwareReport& report);

  void closeButtonPressed() override;

 private:
  /// (Re)creates the content: texts are read when components are built, so a language change rebuilds them.
  void buildContent(bool settingsOpen);

  core::CommandBus& bus_;
  const core::AudioEngineStatsSource& stats_;
  std::shared_ptr<core::ILibrarySource> library_;
  core::ILiveEngineSource* live_;
  const core::IDeckLoadSource* loads_;
  core::IAutomixControl* automix_;
  std::optional<core::HardwareReport> hardwareReport_;
  MainComponent* content_{nullptr};  // owned by the window (JUCE content ownership); valid for its lifetime

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

}  // namespace zyron::ui
