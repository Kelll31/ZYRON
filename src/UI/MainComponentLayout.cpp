// SPDX-License-Identifier: AGPL-3.0-only
// The geometry of MainComponent: which blocks the user can resize, their limits and defaults, and what is remembered.
#include <algorithm>
#include <cmath>

#include "UI/MainComponent.hpp"

namespace zyron::ui {

namespace {
constexpr int kBarThickness = 6;
constexpr int kMinMiddleHeight = 300;  // decks and mixer never shrink below this
constexpr int kMinBottomHeight = 150;
constexpr int kMaxWaveformHeight = 220;
constexpr int kMinDeckWidth = 240;
constexpr int kMinQueueWidth = 380;
constexpr int kMinSettingsWidth = 240;
constexpr int kMaxSettingsWidth = 520;
constexpr int kDefaultSettingsWidth = 300;
}  // namespace

void MainComponent::configureMainLayout(int totalHeight) {
  const bool four = layoutMode_ == LayoutMode::FourDecks;
  const int minWaveform = four ? 64 : 40;
  const int bars = 2 * kBarThickness;
  const int wantedWaveform = layoutSizes_.waveformHeight > 0 ? layoutSizes_.waveformHeight : (four ? 80 : 56);
  const int waveform = std::clamp(wantedWaveform, minWaveform, kMaxWaveformHeight);

  const int defaultBottom = std::clamp((totalHeight - bars) * 34 / 100, 160, 260) + 24;
  const int wantedBottom = layoutSizes_.bottomHeight > 0 ? layoutSizes_.bottomHeight : defaultBottom;
  const int bottom = std::clamp(
      wantedBottom, kMinBottomHeight, std::max(kMinBottomHeight, totalHeight - bars - waveform - kMinMiddleHeight));

  mainLayout_.setItemLayout(0, minWaveform, kMaxWaveformHeight, waveform);
  mainLayout_.setItemLayout(1, kBarThickness, kBarThickness, kBarThickness);
  mainLayout_.setItemLayout(2, kMinMiddleHeight, -1.0,
                            std::max(kMinMiddleHeight, totalHeight - bars - waveform - bottom));
  mainLayout_.setItemLayout(3, kBarThickness, kBarThickness, kBarThickness);
  mainLayout_.setItemLayout(4, kMinBottomHeight, -0.75, bottom);
  mainLayoutReady_ = true;
}

void MainComponent::configureDeckLayout(int totalWidth) {
  const bool four = layoutMode_ == LayoutMode::FourDecks;
  const int minMixer = four ? 240 : 180;
  const int maxMixer = four ? 520 : 420;
  const int defaultMixer =
      four ? std::clamp(totalWidth * 28 / 100, 260, 360) : std::clamp(totalWidth * 22 / 100, 200, 260);
  const int storedMixer = four ? layoutSizes_.mixerWidth4 : layoutSizes_.mixerWidth2;
  const int wantedMixer = storedMixer > 0 ? storedMixer : defaultMixer;
  const int bars = 2 * kBarThickness;
  const int mixer =
      std::clamp(wantedMixer, minMixer, std::max(minMixer, std::min(maxMixer, totalWidth - bars - 2 * kMinDeckWidth)));

  const int percent = layoutSizes_.deckSplitPercent > 0 ? layoutSizes_.deckSplitPercent : 50;
  const int decks = std::max(2 * kMinDeckWidth, totalWidth - bars - mixer);
  const int left = decks * percent / 100;

  deckLayout_.setItemLayout(0, kMinDeckWidth, -1.0, left);
  deckLayout_.setItemLayout(1, kBarThickness, kBarThickness, kBarThickness);
  deckLayout_.setItemLayout(2, minMixer, maxMixer, mixer);
  deckLayout_.setItemLayout(3, kBarThickness, kBarThickness, kBarThickness);
  deckLayout_.setItemLayout(4, kMinDeckWidth, -1.0, decks - left);
  deckLayoutReady_ = true;
}

void MainComponent::configureAutomixLayout(int totalWidth) {
  const int wanted = layoutSizes_.automixSettingsWidth > 0 ? layoutSizes_.automixSettingsWidth : kDefaultSettingsWidth;
  const int maxWidth =
      std::max(kMinSettingsWidth, std::min(kMaxSettingsWidth, totalWidth - kMinQueueWidth - kBarThickness));
  const int settings = std::clamp(wanted, kMinSettingsWidth, maxWidth);
  automixLayout_.setItemLayout(0, kMinQueueWidth, -1.0, totalWidth - kBarThickness - settings);
  automixLayout_.setItemLayout(1, kBarThickness, kBarThickness, kBarThickness);
  automixLayout_.setItemLayout(2, kMinSettingsWidth, kMaxSettingsWidth, settings);
  automixLayoutReady_ = true;
}

void MainComponent::rememberLayout() {
  if (mainLayoutReady_) {
    layoutSizes_.waveformHeight = mainLayout_.getItemCurrentAbsoluteSize(0);
    layoutSizes_.bottomHeight = mainLayout_.getItemCurrentAbsoluteSize(4);
  }
  if (deckLayoutReady_) {
    const int left = deckLayout_.getItemCurrentAbsoluteSize(0);
    const int right = deckLayout_.getItemCurrentAbsoluteSize(4);
    int& mixerWidth = layoutMode_ == LayoutMode::FourDecks ? layoutSizes_.mixerWidth4 : layoutSizes_.mixerWidth2;
    mixerWidth = deckLayout_.getItemCurrentAbsoluteSize(2);
    if (left + right > 0) {
      layoutSizes_.deckSplitPercent = std::clamp(static_cast<int>(std::lround(100.0 * left / (left + right))), 10, 90);
    }
  }
  if (automixLayoutReady_) {
    layoutSizes_.automixSettingsWidth = automixLayout_.getItemCurrentAbsoluteSize(2);
  }
  saveLayoutSizes(layoutSizes_);
}

void MainComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);

  // Top accent line
  g.setColour(theme_.accent);
  g.fillRect(getLocalBounds().removeFromTop(3));
}

void MainComponent::layOutDecks(juce::Rectangle<int> area) {
  if (!deckLayoutReady_ && area.getWidth() > 0) {
    configureDeckLayout(area.getWidth());
  }
  juce::Component* horizontalParts[] = {&leftDecksSlot_, &deckBarLeft_, &mixer_, &deckBarRight_, &rightDecksSlot_};
  deckLayout_.layOutComponents(horizontalParts, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false,
                               true);
  auto left = leftDecksSlot_.getBounds();
  auto right = rightDecksSlot_.getBounds();

  if (layoutMode_ == LayoutMode::FourDecks) {
    // SPEC section 62: left column Deck A over Deck C, mixer in the centre, right column Deck B over Deck D.
    const int deckH = (area.getHeight() - 4) / 2;
    deckA_.setBounds(left.removeFromTop(deckH));
    left.removeFromTop(4);
    deckC_.setBounds(left);
    deckB_.setBounds(right.removeFromTop(deckH));
    right.removeFromTop(4);
    deckD_.setBounds(right);
  } else {
    deckA_.setBounds(left);
    deckB_.setBounds(right);
  }
}

void MainComponent::resized() {
  auto area = getLocalBounds().reduced(8);
  area.removeFromTop(3);  // accent line

  // 1. Top Bar (Height 36px)
  auto topBar = area.removeFromTop(36);
  logo_.setBounds(topBar.removeFromLeft(logo_.preferredWidth(topBar.getHeight()) + 12));

  auto modesArea = topBar.removeFromLeft(200);
  view2DecksBtn_.setBounds(modesArea.removeFromLeft(95).reduced(2, 4));
  view4DecksBtn_.setBounds(modesArea.reduced(2, 4));

  recBtn_.setBounds(topBar.removeFromLeft(75).reduced(2, 4));
  automixBtn_.setBounds(topBar.removeFromLeft(90).reduced(2, 4));
  settingsBtn_.setBounds(topBar.removeFromLeft(90).reduced(2, 4));

  statsLabel_.setBounds(topBar.reduced(4, 2));

  area.removeFromTop(6);

  // If Settings Drawer is opened:
  if (showSettings_) {
    settingsTabs_.setBounds(area);
    return;
  }

  // FX hit pads: a thin strip under the top bar, reachable in every layout.
  fxHits_.setBounds(area.removeFromTop(FxHitsBar::kPreferredHeight));
  area.removeFromTop(4);

  // 2. Global waveform | bar | decks and mixer | bar | library or Automix. The sizes are whatever the user dragged
  // them to (remembered between runs); the first layout starts from defaults.
  if (!mainLayoutReady_ && area.getHeight() > 0) {
    configureMainLayout(area.getHeight());
  }
  juce::Component* verticalParts[] = {&waveformSlot_, &waveformBar_, &middleSlot_, &mainBar_, &bottomSlot_};
  mainLayout_.layOutComponents(verticalParts, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true,
                               true);
  globalWaveform_.setBounds(waveformSlot_.getBounds());
  auto bottom = bottomSlot_.getBounds();
  area = middleSlot_.getBounds();

  auto tabs = bottom.removeFromTop(24);
  libraryTabBtn_.setBounds(tabs.removeFromLeft(110).reduced(1, 1));
  queueTabBtn_.setBounds(tabs.removeFromLeft(140).reduced(1, 1));
  library_.setBounds(bottom);
  queue_.setBounds(bottom);
  if (showQueue_) {
    // Automix workspace: queue | draggable bar | settings
    if (!automixLayoutReady_ && bottom.getWidth() > 0) {
      configureAutomixLayout(bottom.getWidth());
    }
    juce::Component* horizontalParts[] = {&queue_, &automixBar_, &automixView_};
    automixLayout_.layOutComponents(horizontalParts, 3, bottom.getX(), bottom.getY(), bottom.getWidth(),
                                    bottom.getHeight(), false, true);
    automixSettings_.setSize(automixView_.getMaximumVisibleWidth(),
                             std::max(AutomixSettingsPanel::kPreferredHeight, automixView_.getMaximumVisibleHeight()));
  }

  // 3. Middle DJ workstation
  layOutDecks(area);
}

}  // namespace zyron::ui
