// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BrandingData.h"

namespace zyron::ui {

/// The ZYRON logo for the top bar: the Z mark with the ZYRON wordmark next to it, built from the images embedded in
/// the executable (resources/branding). Transparent PNGs in a light tone, made for the dark theme.
class LogoComponent final : public juce::Component {
 public:
  LogoComponent()
      : mark_(juce::ImageCache::getFromMemory(zyron_branding::zyronmark_png, zyron_branding::zyronmark_pngSize)),
        wordmark_(juce::ImageCache::getFromMemory(zyron_branding::zyronwordmark_png,
                                                  zyron_branding::zyronwordmark_pngSize)) {
    setInterceptsMouseClicks(false, false);
  }

  /// The width that keeps the proportions at the given height.
  [[nodiscard]] int preferredWidth(int height) const {
    if (!mark_.isValid() || !wordmark_.isValid()) {
      return height;
    }
    const float markHeight = static_cast<float>(height) * kMarkShare;
    const float markWidth = markHeight * static_cast<float>(mark_.getWidth()) / static_cast<float>(mark_.getHeight());
    const float wordHeight = markHeight * kWordShare;
    const float wordWidth = wordHeight * static_cast<float>(wordmark_.getWidth()) / static_cast<float>(wordmark_.getHeight());
    return static_cast<int>(markWidth + kGap + wordWidth) + 4;
  }

  void paint(juce::Graphics& g) override {
    if (!mark_.isValid() || !wordmark_.isValid()) {
      return;
    }
    const float height = static_cast<float>(getHeight());
    const float markHeight = height * kMarkShare;
    const float markWidth = markHeight * static_cast<float>(mark_.getWidth()) / static_cast<float>(mark_.getHeight());
    const float wordHeight = markHeight * kWordShare;
    const float wordWidth = wordHeight * static_cast<float>(wordmark_.getWidth()) / static_cast<float>(wordmark_.getHeight());

    const float top = (height - markHeight) * 0.5f;
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(mark_, juce::Rectangle<float>(2.0f, top, markWidth, markHeight),
                juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    g.drawImage(wordmark_,
                juce::Rectangle<float>(2.0f + markWidth + kGap, (height - wordHeight) * 0.5f, wordWidth, wordHeight),
                juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
  }

 private:
  static constexpr float kMarkShare = 0.86f;  // of the bar height
  static constexpr float kWordShare = 0.34f;  // of the mark height
  static constexpr float kGap = 8.0f;

  juce::Image mark_;
  juce::Image wordmark_;
};

}  // namespace zyron::ui
