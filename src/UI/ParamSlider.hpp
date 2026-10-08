// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

namespace zyron::ui {

/// A slider or knob with the two DJ-software conventions (used for every parameter of the mixer and the decks):
///  - right-click puts the parameter back to its default value;
///  - double-click opens a small dialog to type an exact value (clamped to the range).
/// Dragging, the mouse wheel and the keyboard keep JUCE's behaviour.
class ParamSlider : public juce::Slider {
 public:
  ParamSlider() { setTooltip(TRANS("Right-click: reset to default | Double-click: type a value")); }

  /// The value a right-click restores. Call after setRange().
  void setDefaultValue(double value) noexcept { defaultValue_ = value; }
  [[nodiscard]] double defaultValue() const noexcept { return defaultValue_; }

  /// Name shown in the dialog title (for example "Channel A gain (dB)").
  void setParameterName(const juce::String& name) { parameterName_ = name; }

  /// Shows a value that changed without the user touching this control (Automix, MIDI, the AI). Silent: it never
  /// sends a Command back. Ignored while the user holds the control, so the hand always wins.
  void showExternalValue(double value) {
    if (!isMouseButtonDown(true) && std::abs(getValue() - value) > 1.0e-6) {
      setValue(value, juce::dontSendNotification);
    }
  }

  /// Restores the default value and notifies listeners like a user change.
  void resetToDefault() { setValue(defaultValue_, juce::sendNotificationSync); }

  /// Applies typed text: a number (a comma is accepted as the decimal separator), clamped to the range. Returns false
  /// when the text is not a number.
  bool setFromText(const juce::String& text) {
    const juce::String cleaned = text.trim().replaceCharacter(',', '.');
    if (cleaned.isEmpty() || !cleaned.containsOnly("+-.0123456789eE")) {
      return false;
    }
    const double typed = cleaned.getDoubleValue();
    setValue(std::clamp(typed, getMinimum(), getMaximum()), juce::sendNotificationSync);
    return true;
  }

  void mouseDown(const juce::MouseEvent& event) override {
    if (event.mods.isPopupMenu()) {
      resetToDefault();
      return;  // not a drag: JUCE must not start one
    }
    juce::Slider::mouseDown(event);
  }
  void mouseDrag(const juce::MouseEvent& event) override {
    if (!event.mods.isPopupMenu()) {
      juce::Slider::mouseDrag(event);
    }
  }
  void mouseUp(const juce::MouseEvent& event) override {
    if (!event.mods.isPopupMenu()) {
      juce::Slider::mouseUp(event);
    }
  }

  void mouseDoubleClick(const juce::MouseEvent& event) override {
    if (event.mods.isPopupMenu() || !isEnabled()) {
      return;
    }
    showValueDialog();
  }

 private:
  void showValueDialog() {
    auto* dialog = new juce::AlertWindow(parameterName_.isEmpty() ? juce::String(TRANS("Set value")) : parameterName_,
                                         juce::String(TRANS("Range %min% to %max%, default %def%"))
                                             .replace("%min%", juce::String(getMinimum()))
                                             .replace("%max%", juce::String(getMaximum()))
                                             .replace("%def%", juce::String(defaultValue_)),
                                         juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("value", juce::String(getValue()), TRANS("Value"));
    dialog->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(TRANS("Cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<ParamSlider>(this), dialog](int result) {
          if (result == 1 && safe != nullptr) {
            (void)safe->setFromText(dialog->getTextEditorContents("value"));
          }
        }),
        true);
  }

  double defaultValue_{0.0};
  juce::String parameterName_;
};

}  // namespace zyron::ui
