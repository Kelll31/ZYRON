// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "UI/Theme.hpp"

namespace zyron::ui {

/// The draggable edge between two blocks (a StretchableLayoutResizerBar drawn with the theme). `onMoved` fires after
/// every drag step, once the layout has been re-applied, so the owner can remember the new sizes.
class SplitterBar final : public juce::StretchableLayoutResizerBar, public juce::SettableTooltipClient {
 public:
  SplitterBar(juce::StretchableLayoutManager* layout, int itemIndexInLayout, bool isVertical, const Theme& theme)
      : juce::StretchableLayoutResizerBar(layout, itemIndexInLayout, isVertical), theme_(theme), vertical_(isVertical) {
    setTooltip(TRANS("Drag to resize | Right-click: default size"));
  }

  void setTheme(const Theme& theme) {
    theme_ = theme;
    repaint();
  }

  std::function<void()> onMoved;
  /// Right-click: the owner puts this edge back to its default position.
  std::function<void()> onReset;

  void mouseDown(const juce::MouseEvent& e) override {
    if (e.mods.isPopupMenu()) {
      if (onReset) onReset();
      return;  // not the start of a drag
    }
    juce::StretchableLayoutResizerBar::mouseDown(e);
  }
  void mouseDrag(const juce::MouseEvent& e) override {
    if (!e.mods.isPopupMenu()) {
      juce::StretchableLayoutResizerBar::mouseDrag(e);
    }
  }

  void paint(juce::Graphics& g) override {
    const bool active = isMouseOverOrDragging();
    g.setColour(active ? theme_.accent.withAlpha(0.35f) : theme_.panel);
    g.fillAll();
    // Three grip dots centred on the bar, so it reads as draggable
    g.setColour(active ? theme_.accent : theme_.textDim);
    const auto centre = getLocalBounds().toFloat().getCentre();
    for (int i = -1; i <= 1; ++i) {
      const float offset = static_cast<float>(i) * 6.0f;
      g.fillEllipse(centre.x + (vertical_ ? 0.0f : offset) - 1.5f, centre.y + (vertical_ ? offset : 0.0f) - 1.5f, 3.0f,
                    3.0f);
    }
  }

  void mouseEnter(const juce::MouseEvent& e) override {
    juce::StretchableLayoutResizerBar::mouseEnter(e);
    repaint();
  }
  void mouseExit(const juce::MouseEvent& e) override {
    juce::StretchableLayoutResizerBar::mouseExit(e);
    repaint();
  }

  void hasBeenMoved() override {
    juce::StretchableLayoutResizerBar::hasBeenMoved();  // re-applies the parent's layout
    if (onMoved) {
      onMoved();
    }
  }

 private:
  Theme theme_;
  bool vertical_;
};

}  // namespace zyron::ui
