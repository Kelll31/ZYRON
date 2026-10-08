// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/System/HardwareInfo.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Read-only text view of the hardware report (ROADMAP P1-04). It will grow into the diagnostics page that shows
/// xruns, queue drops, DSP load and GPU/VRAM use (ARCHITECTURE section 12).
class DiagnosticsPanel final : public juce::Component {
 public:
  explicit DiagnosticsPanel(const Theme& theme);

  void setReport(const core::HardwareReport& report);
  [[nodiscard]] juce::String text() const { return view_.getText(); }

  void resized() override;

 private:
  juce::TextEditor view_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiagnosticsPanel)
};

}  // namespace zyron::ui
