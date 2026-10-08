// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_graphics/juce_graphics.h>

#include "Core/State/Ids.hpp"

namespace zyron::ui {

/// Colour tokens for the whole UI (SPEC section 63). Components read colours from here and never hard-code them.
/// Dark is the primary theme; Light is supported.
struct Theme {
  juce::Colour background;
  juce::Colour panel;
  juce::Colour text;
  juce::Colour textDim;
  juce::Colour accent;

  juce::Colour waveformBackground{0xff0d0f12};
  juce::Colour waveformLow{0xffff453a};
  juce::Colour waveformMid{0xff32d74b};
  juce::Colour waveformHigh{0xff64d2ff};
  juce::Colour waveformPlayhead{0xffffffff};
  juce::Colour beatgridLine{0x66ffffff};
  juce::Colour beatgridDownbeat{0xddff453a};
  juce::Colour loopRegion{0x443ddc97};

  juce::Colour deckA{0xff00d2ff};
  juce::Colour deckB{0xffff8c00};
  juce::Colour deckC{0xffbf5af2};
  juce::Colour deckD{0xffffd60a};
  juce::Colour cueActive{0xffff9500};
  juce::Colour playActive{0xff32d74b};
  juce::Colour syncActive{0xff0a84ff};
  juce::Colour meterGreen{0xff32d74b};
  juce::Colour meterYellow{0xffffd60a};
  juce::Colour meterRed{0xffff453a};

  [[nodiscard]] juce::Colour deckColour(core::DeckId id) const noexcept {
    switch (id) {
      case core::DeckId::A:
        return deckA;
      case core::DeckId::B:
        return deckB;
      case core::DeckId::C:
        return deckC;
      case core::DeckId::D:
        return deckD;
    }
    return deckA;
  }

  [[nodiscard]] static Theme dark() {
    Theme t;
    t.background = juce::Colour{0xff101216};
    t.panel = juce::Colour{0xff1a1d23};
    t.text = juce::Colour{0xffe8eaed};
    t.textDim = juce::Colour{0xff8b919b};
    t.accent = juce::Colour{0xff3ddc97};
    t.waveformBackground = juce::Colour{0xff0d0f12};
    t.waveformLow = juce::Colour{0xffff453a};
    t.waveformMid = juce::Colour{0xff32d74b};
    t.waveformHigh = juce::Colour{0xff64d2ff};
    t.waveformPlayhead = juce::Colour{0xffffffff};
    t.beatgridLine = juce::Colour{0x66ffffff};
    t.beatgridDownbeat = juce::Colour{0xddff453a};
    t.loopRegion = juce::Colour{0x443ddc97};
    t.deckA = juce::Colour{0xff00d2ff};
    t.deckB = juce::Colour{0xffff8c00};
    t.deckC = juce::Colour{0xffbf5af2};
    t.deckD = juce::Colour{0xffffd60a};
    t.cueActive = juce::Colour{0xffff9500};
    t.playActive = juce::Colour{0xff32d74b};
    t.syncActive = juce::Colour{0xff0a84ff};
    t.meterGreen = juce::Colour{0xff32d74b};
    t.meterYellow = juce::Colour{0xffffd60a};
    t.meterRed = juce::Colour{0xffff453a};
    return t;
  }

  [[nodiscard]] static Theme light() {
    Theme t;
    t.background = juce::Colour{0xfff4f5f7};
    t.panel = juce::Colour{0xffffffff};
    t.text = juce::Colour{0xff16181d};
    t.textDim = juce::Colour{0xff5d6470};
    t.accent = juce::Colour{0xff0c8f5f};
    t.waveformBackground = juce::Colour{0xffe5e7eb};
    t.waveformLow = juce::Colour{0xffd70015};
    t.waveformMid = juce::Colour{0xff248a3d};
    t.waveformHigh = juce::Colour{0xff0071a4};
    t.waveformPlayhead = juce::Colour{0xff000000};
    t.beatgridLine = juce::Colour{0x66000000};
    t.beatgridDownbeat = juce::Colour{0xddd70015};
    t.loopRegion = juce::Colour{0x440c8f5f};
    t.deckA = juce::Colour{0xff0071a4};
    t.deckB = juce::Colour{0xffd95700};
    t.deckC = juce::Colour{0xff8944ab};
    t.deckD = juce::Colour{0xffca8a04};
    t.cueActive = juce::Colour{0xffd97706};
    t.playActive = juce::Colour{0xff248a3d};
    t.syncActive = juce::Colour{0xff0284c7};
    t.meterGreen = juce::Colour{0xff248a3d};
    t.meterYellow = juce::Colour{0xffca8a04};
    t.meterRed = juce::Colour{0xffdc2626};
    return t;
  }
};

}  // namespace zyron::ui
