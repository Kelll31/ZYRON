// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Automix/AutomixQueueComponent.hpp"
#include "UI/Automix/TransitionLabels.hpp"
#include "UI/Localization.hpp"

#include <cmath>
#include <iterator>
#include <cstdio>

namespace zyron::ui {

namespace {

juce::String formatTime(double seconds) {
  if (seconds < 0.0) {
    return "--";
  }
  const int total = static_cast<int>(seconds);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
  return buffer;
}

}  // namespace

AutomixQueueComponent::AutomixQueueComponent(Theme theme) : theme_(theme) {
  summaryLabel_.setFont(juce::FontOptions(12.0f));
  summaryLabel_.setColour(juce::Label::textColourId, theme_.textDim);
  summaryLabel_.setText(TRANS("Automix is off: press AUTOMIX and the AI plans a set from the analysed tracks."),
                        juce::dontSendNotification);
  addAndMakeVisible(summaryLabel_);

  for (auto* button : {&moveUpButton_, &moveDownButton_, &removeButton_, &jumpButton_}) {
    button->setColour(juce::TextButton::buttonColourId, theme_.panel);
    button->setColour(juce::TextButton::textColourOffId, theme_.accent);
    addAndMakeVisible(*button);
  }
  jumpButton_.setTooltip(TRANS("Move the playing track to a little before the transition the AI planned, to hear the mix"));
  jumpButton_.onClick = [this] {
    if (onJumpToTransitionRequested) onJumpToTransitionRequested();
  };
  moveUpButton_.setTooltip(TRANS("Play the selected track earlier (only tracks that have not started)"));
  moveDownButton_.setTooltip(TRANS("Play the selected track later"));
  removeButton_.setTooltip(TRANS("Take the selected track out of the set"));

  moveUpButton_.onClick = [this] {
    const int row = table_.getSelectedRow();
    if (row > 0 && onMoveRequested) onMoveRequested(static_cast<std::size_t>(row), static_cast<std::size_t>(row - 1));
  };
  moveDownButton_.onClick = [this] {
    const int row = table_.getSelectedRow();
    if (row >= 0 && onMoveRequested) onMoveRequested(static_cast<std::size_t>(row), static_cast<std::size_t>(row + 1));
  };
  removeButton_.onClick = [this] {
    const int row = table_.getSelectedRow();
    if (row >= 0 && onRemoveRequested) onRemoveRequested(static_cast<std::size_t>(row));
  };

  constexpr int columnFlags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable;
  auto& header = table_.getHeader();
  header.addColumn("#", ColIndex, 30, 24, 40, columnFlags);
  header.addColumn(TRANS("Title"), ColTitle, 220, 100, 400, columnFlags);
  header.addColumn(TRANS("Artist"), ColArtist, 140, 80, 260, columnFlags);
  header.addColumn("BPM", ColBpm, 56, 44, 80, columnFlags);
  header.addColumn("Key", ColKey, 48, 40, 70, columnFlags);
  header.addColumn(TRANS("Energy"), ColEnergy, 56, 44, 80, columnFlags);
  header.addColumn(TRANS("Fit"), ColFit, 56, 44, 80, columnFlags);
  header.addColumn(TRANS("Transition"), ColTransition, 110, 70, 180, columnFlags);
  header.addColumn(TRANS("Mix in"), ColMixIn, 56, 44, 80, columnFlags);
  header.addColumn(TRANS("Mix out"), ColMixOut, 60, 44, 80, columnFlags);
  header.addColumn(TRANS("Drop"), ColDrop, 52, 44, 80, columnFlags);
  header.addColumn(TRANS("Why the AI chose it"), ColWhy, 480, 200, 1200, columnFlags);

  table_.setModel(this);
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  table_.setOutlineThickness(0);
  addAndMakeVisible(table_);
}

AutomixQueueComponent::~AutomixQueueComponent() {
  table_.setModel(nullptr);
}

void AutomixQueueComponent::setTheme(const Theme& theme) {
  theme_ = theme;
  table_.setColour(juce::ListBox::backgroundColourId, theme_.background);
  repaint();
}

bool AutomixQueueComponent::sameQueue(const core::AutomixQueue& a, const core::AutomixQueue& b) {
  if (a.current != b.current || a.running != b.running || a.entries.size() != b.entries.size() ||
      a.summary != b.summary) {
    return false;
  }
  for (std::size_t i = 0; i < a.entries.size(); ++i) {
    const auto& x = a.entries[i];
    const auto& y = b.entries[i];
    if (x.track.id != y.track.id || x.track.bpm != y.track.bpm || x.points.mixInSec != y.points.mixInSec ||
        x.points.mixOutSec != y.points.mixOutSec || x.points.dropSec != y.points.dropSec || x.transitionIn != y.transitionIn ||
        x.transitionChosen != y.transitionChosen) {
      return false;
    }
  }
  return true;
}

void AutomixQueueComponent::update(const core::AutomixQueue& queue) {
  if (sameQueue(queue, queue_)) {
    return;
  }
  const int selected = table_.getSelectedRow();
  queue_ = queue;
  table_.updateContent();
  if (selected >= 0 && selected < getNumRows()) {
    table_.selectRow(selected);
  }

  if (queue_.entries.empty()) {
    summaryLabel_.setText(TRANS("Automix is off: press AUTOMIX and the AI plans a set from the analysed tracks."),
                          juce::dontSendNotification);
  } else {
    juce::String text = queue_.summary.empty() ? juce::String(TRANS("Planned set")) : juce::String(queue_.summary);
    text << "  |  "
         << juce::String(queue_.running ? TRANS("playing %a of %b") : TRANS("stopped at %a of %b"))
                .replace("%a", juce::String(static_cast<int>(queue_.current + 1)))
                .replace("%b", juce::String(static_cast<int>(queue_.entries.size())));
    summaryLabel_.setText(text, juce::dontSendNotification);
  }
  repaint();
}

bool AutomixQueueComponent::isUpcoming(int row) const {
  return row >= 0 && static_cast<std::size_t>(row) > queue_.current + 1;
}

int AutomixQueueComponent::getNumRows() {
  return static_cast<int>(queue_.entries.size());
}

void AutomixQueueComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int, int, bool rowIsSelected) {
  const auto index = static_cast<std::size_t>(rowNumber);
  if (queue_.running && index == queue_.current) {
    g.fillAll(theme_.playActive.withAlpha(0.22f));
  } else if (queue_.running && index == queue_.current + 1) {
    g.fillAll(theme_.accent.withAlpha(0.14f));
  } else if (rowIsSelected) {
    g.fillAll(theme_.accent.withAlpha(0.25f));
  } else if (rowNumber % 2 == 1) {
    g.fillAll(theme_.panel.withAlpha(0.35f));
  }
}

void AutomixQueueComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
  if (rowNumber < 0 || rowNumber >= getNumRows()) {
    return;
  }
  const auto index = static_cast<std::size_t>(rowNumber);
  const auto& entry = queue_.entries[index];
  const bool played = queue_.running && index < queue_.current;
  g.setColour(played ? theme_.textDim.withAlpha(0.6f) : theme_.text);
  g.setFont(12.0f);
  const auto bounds = juce::Rectangle<int>(4, 0, width - 8, height);

  switch (columnId) {
    case ColIndex:
      g.drawText(juce::String(rowNumber + 1), bounds, juce::Justification::centredRight, true);
      break;
    case ColTitle: {
      g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
      juce::String title = entry.track.title.empty() ? juce::String(TRANS("(Untitled)")) : juce::String(entry.track.title);
      if (queue_.running && index == queue_.current) {
        title = "> " + title;
      } else if (queue_.running && index == queue_.current + 1) {
        title = juce::String(TRANS("next")) + ": " + title;
      }
      g.drawText(title, bounds, juce::Justification::centredLeft, true);
      break;
    }
    case ColArtist:
      g.drawText(entry.track.artist, bounds, juce::Justification::centredLeft, true);
      break;
    case ColBpm:
      g.drawText(entry.track.bpm > 0.0 ? juce::String(entry.track.bpm, 1) : juce::String("--"), bounds,
                 juce::Justification::centredRight, true);
      break;
    case ColKey:
      g.drawText(entry.track.key.empty() ? juce::String("--") : juce::String(entry.track.key), bounds,
                 juce::Justification::centred, true);
      break;
    case ColEnergy:
      g.drawText(entry.track.energy > 0.0 ? juce::String(entry.track.energy, 1) : juce::String("--"), bounds,
                 juce::Justification::centred, true);
      break;
    case ColFit: {
      const float fit = entry.fit;
      g.setColour(index == 0 ? theme_.textDim
                             : (fit >= 0.75f ? theme_.playActive : (fit >= 0.5f ? theme_.accent : theme_.meterRed)));
      g.drawText(index == 0 ? juce::String("--") : juce::String(static_cast<int>(std::lround(fit * 100.0f))) + "%",
                 bounds, juce::Justification::centred, true);
      break;
    }
    case ColTransition: {
      if (index == 0) {  // nothing is mixed into the opening track
        g.setColour(theme_.textDim);
        g.drawText("--", bounds, juce::Justification::centred, true);
        break;
      }
      juce::String label = transitionStyleLabel(entry.transitionIn);
      if (entry.transitionChosen) {  // picked by the DJ: bold with a dot, so it stands out from the automatic rotation
        g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
        g.setColour(theme_.accent);
        label = juce::String::charToString(0x25CF) + " " + label;
      }
      g.drawText(label, bounds, juce::Justification::centredLeft, true);
      break;
    }
    case ColMixIn:
      g.drawText(formatTime(entry.points.mixInSec), bounds, juce::Justification::centred, true);
      break;
    case ColMixOut:
      g.drawText(formatTime(entry.points.mixOutSec), bounds, juce::Justification::centred, true);
      break;
    case ColDrop:
      g.drawText(formatTime(entry.points.dropSec), bounds, juce::Justification::centred, true);
      break;
    case ColWhy:
      g.setColour(theme_.textDim);
      g.drawText(index == 0 ? juce::String(TRANS("Opening track chosen for the start of the energy curve"))
                            : i18n::translateMessage(entry.reason),
                 bounds, juce::Justification::centredLeft, true);
      break;
    default:
      break;
  }
}

void AutomixQueueComponent::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) {
  if (columnId == ColTransition || e.mods.isPopupMenu()) {
    showTransitionMenu(rowNumber);
  }
}

void AutomixQueueComponent::showTransitionMenu(int row) {
  if (row <= 0 || row >= getNumRows()) {  // the first track has no incoming transition
    return;
  }
  const auto& entry = queue_.entries[static_cast<std::size_t>(row)];
  // The styles the DJ can ask for; BassSwap is shown as "Blend", QuickCut as "Cut on the drop".
  static constexpr core::TransitionStyle kChoices[] = {
      core::TransitionStyle::BassSwap, core::TransitionStyle::FilterFade, core::TransitionStyle::LoopRoll,
      core::TransitionStyle::Brake,    core::TransitionStyle::Scratch,    core::TransitionStyle::QuickCut,
      core::TransitionStyle::BeatLoopIn, core::TransitionStyle::DoubleDrop, core::TransitionStyle::EchoOut,
      core::TransitionStyle::ReverbOut, core::TransitionStyle::StemBlend};
  juce::PopupMenu menu;
  menu.addItem(1, TRANS("Auto (profile)"), true, !entry.transitionChosen);
  menu.addSeparator();
  for (std::size_t i = 0; i < std::size(kChoices); ++i) {
    menu.addItem(static_cast<int>(i) + 2, transitionStyleLabel(kChoices[i]), true,
                 entry.transitionChosen && entry.transitionIn == kChoices[i]);
  }
  juce::Component::SafePointer<AutomixQueueComponent> safe(this);
  menu.showMenuAsync(juce::PopupMenu::Options(), [safe, row](int result) {
    if (safe == nullptr || result <= 0 || !safe->onTransitionChosen) {
      return;
    }
    std::optional<core::TransitionStyle> choice;  // empty: Auto
    if (result > 1) {
      choice = kChoices[static_cast<std::size_t>(result - 2)];
    }
    safe->onTransitionChosen(static_cast<std::size_t>(row), choice);
  });
}

void AutomixQueueComponent::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);
}

void AutomixQueueComponent::resized() {
  auto area = getLocalBounds().reduced(6);
  auto top = area.removeFromTop(28);
  jumpButton_.setBounds(top.removeFromRight(130));
  top.removeFromRight(8);
  removeButton_.setBounds(top.removeFromRight(80));
  top.removeFromRight(4);
  moveDownButton_.setBounds(top.removeFromRight(60));
  top.removeFromRight(4);
  moveUpButton_.setBounds(top.removeFromRight(60));
  top.removeFromRight(8);
  summaryLabel_.setBounds(top);
  area.removeFromTop(4);
  table_.setBounds(area);
}

}  // namespace zyron::ui
