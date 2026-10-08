// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Waveform/GlobalWaveformComponent.hpp"

namespace zyron::ui {

GlobalWaveformComponent::GlobalWaveformComponent(Theme theme)
    : theme_(theme),
      waveformA_(theme),
      waveformB_(theme),
      waveformC_(theme),
      waveformD_(theme) {
  auto setupBadge = [this](juce::Label& badge, const juce::String& text, juce::Colour col) {
    badge.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    badge.setJustificationType(juce::Justification::centred);
    badge.setText(text, juce::dontSendNotification);
    badge.setColour(juce::Label::textColourId, col);
    addAndMakeVisible(badge);
  };

  setupBadge(badgeA_, "A", theme_.deckA);
  setupBadge(badgeB_, "B", theme_.deckB);
  setupBadge(badgeC_, "C", theme_.deckC);
  setupBadge(badgeD_, "D", theme_.deckD);

  auto setupWaveform = [this](WaveformView& wf, core::DeckId deck) {
    wf.setMode(WaveformView::Mode::OverviewOnly);
    wf.onSeekRequested = [this, deck](double sec) {
      if (onSeekRequested) {
        onSeekRequested(deck, sec);
      }
    };
    addAndMakeVisible(wf);
  };

  setupWaveform(waveformA_, core::DeckId::A);
  setupWaveform(waveformB_, core::DeckId::B);
  setupWaveform(waveformC_, core::DeckId::C);
  setupWaveform(waveformD_, core::DeckId::D);

  setLayoutMode(LayoutMode::TwoDecks);
}

void GlobalWaveformComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  badgeA_.setColour(juce::Label::textColourId, theme_.deckA);
  badgeB_.setColour(juce::Label::textColourId, theme_.deckB);
  badgeC_.setColour(juce::Label::textColourId, theme_.deckC);
  badgeD_.setColour(juce::Label::textColourId, theme_.deckD);

  waveformA_.setTheme(theme);
  waveformB_.setTheme(theme);
  waveformC_.setTheme(theme);
  waveformD_.setTheme(theme);

  repaint();
}

void GlobalWaveformComponent::setLayoutMode(LayoutMode mode) {
  layoutMode_ = mode;
  const bool showCD = (mode == LayoutMode::FourDecks);

  badgeC_.setVisible(showCD);
  badgeD_.setVisible(showCD);
  waveformC_.setVisible(showCD);
  waveformD_.setVisible(showCD);

  resized();
  repaint();
}

void GlobalWaveformComponent::setWaveformData(core::DeckId deck, core::WaveformData data) {
  switch (deck) {
    case core::DeckId::A:
      waveformA_.setWaveformData(std::move(data));
      break;
    case core::DeckId::B:
      waveformB_.setWaveformData(std::move(data));
      break;
    case core::DeckId::C:
      waveformC_.setWaveformData(std::move(data));
      break;
    case core::DeckId::D:
      waveformD_.setWaveformData(std::move(data));
      break;
  }
}

void GlobalWaveformComponent::updateTelemetry(core::DeckId deck, const core::DeckTelemetry& telemetry) {
  switch (deck) {
    case core::DeckId::A:
      waveformA_.updateTelemetry(telemetry);
      break;
    case core::DeckId::B:
      waveformB_.updateTelemetry(telemetry);
      break;
    case core::DeckId::C:
      waveformC_.updateTelemetry(telemetry);
      break;
    case core::DeckId::D:
      waveformD_.updateTelemetry(telemetry);
      break;
  }
}

WaveformView& GlobalWaveformComponent::waveformView(core::DeckId deck) {
  switch (deck) {
    case core::DeckId::A:
      return waveformA_;
    case core::DeckId::B:
      return waveformB_;
    case core::DeckId::C:
      return waveformC_;
    case core::DeckId::D:
      return waveformD_;
  }
  return waveformA_;
}

void GlobalWaveformComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);

  // Background badge strip
  const auto badgeBounds = getLocalBounds().removeFromLeft(28);
  g.setColour(theme_.panel);
  g.fillRect(badgeBounds);

  // Subtle separator lines
  g.setColour(theme_.panel.brighter(0.1f));
  const int count = (layoutMode_ == LayoutMode::FourDecks) ? 4 : 2;
  const float slotH = static_cast<float>(getHeight()) / static_cast<float>(count);

  for (int i = 1; i < count; ++i) {
    const int y = static_cast<int>(slotH * static_cast<float>(i));
    g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
  }

  // Left strip right border
  g.drawVerticalLine(28, 0.0f, static_cast<float>(getHeight()));
}

void GlobalWaveformComponent::resized() {
  auto bounds = getLocalBounds();
  auto badgeArea = bounds.removeFromLeft(28);

  if (layoutMode_ == LayoutMode::FourDecks) {
    const int slotH = bounds.getHeight() / 4;
    badgeA_.setBounds(badgeArea.removeFromTop(slotH));
    waveformA_.setBounds(bounds.removeFromTop(slotH));

    badgeB_.setBounds(badgeArea.removeFromTop(slotH));
    waveformB_.setBounds(bounds.removeFromTop(slotH));

    badgeC_.setBounds(badgeArea.removeFromTop(slotH));
    waveformC_.setBounds(bounds.removeFromTop(slotH));

    badgeD_.setBounds(badgeArea);
    waveformD_.setBounds(bounds);
  } else {
    const int halfH = bounds.getHeight() / 2;
    badgeA_.setBounds(badgeArea.removeFromTop(halfH));
    badgeB_.setBounds(badgeArea);

    waveformA_.setBounds(bounds.removeFromTop(halfH));
    waveformB_.setBounds(bounds);
  }
}

}  // namespace zyron::ui
