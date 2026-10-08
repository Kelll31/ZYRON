// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/DiagnosticsPanel.hpp"

namespace zyron::ui {

DiagnosticsPanel::DiagnosticsPanel(const Theme& theme) {
  view_.setMultiLine(true, false);
  view_.setReadOnly(true);
  view_.setCaretVisible(false);
  view_.setScrollbarsShown(true);
  view_.setFont(juce::FontOptions{juce::Font::getDefaultMonospacedFontName(), 14.0F, juce::Font::plain});
  view_.setColour(juce::TextEditor::backgroundColourId, theme.panel);
  view_.setColour(juce::TextEditor::textColourId, theme.text);
  view_.setColour(juce::TextEditor::outlineColourId, theme.textDim.withAlpha(0.3F));
  view_.setColour(juce::TextEditor::focusedOutlineColourId, theme.accent);
  view_.setText(TRANS("Detecting hardware..."), false);
  addAndMakeVisible(view_);
}

void DiagnosticsPanel::setReport(const core::HardwareReport& report) {
  view_.setText(juce::String(core::formatReport(report)), false);
}

void DiagnosticsPanel::resized() {
  view_.setBounds(getLocalBounds());
}

}  // namespace zyron::ui
