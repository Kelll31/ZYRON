// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/AudioSettingsPanel.hpp"

#include <algorithm>

namespace zyron::ui {
namespace {

constexpr int kSystemDefaultItemId = 1;
constexpr float kToneFrequencyHz = 440.0F;
constexpr double kSliderMinDb = -60.0;
constexpr double kSliderMaxDb = -6.0;  // the Command API allows up to 0 dB; the UI stops well short of ear damage
constexpr double kSliderDefaultDb = -30.0;
constexpr int kStatsRefreshHz = 4;
const core::CommandOrigin kOrigin{core::CommandOrigin::Kind::Ui, "audio-settings"};

void styleLabel(juce::Label& label, const Theme& theme, juce::Colour colour) {
  label.setColour(juce::Label::textColourId, colour);
  label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
  (void)theme;
}

}  // namespace

AudioSettingsPanel::AudioSettingsPanel(const Theme& theme, core::CommandBus& bus,
                                       const core::AudioEngineStatsSource& stats)
    : theme_(theme), bus_(bus), stats_(stats) {
  for (juce::Label* label : {&apiLabel_, &deviceLabel_, &rateLabel_, &bufferLabel_}) {
    styleLabel(*label, theme_, theme_.textDim);
    addAndMakeVisible(*label);
  }
  for (juce::ComboBox* box : {&apiBox_, &deviceBox_, &rateBox_, &bufferBox_}) {
    box->setColour(juce::ComboBox::backgroundColourId, theme_.panel);
    box->setColour(juce::ComboBox::textColourId, theme_.text);
    box->setColour(juce::ComboBox::outlineColourId, theme_.textDim.withAlpha(0.4F));
    addAndMakeVisible(*box);
  }

  int id = 1;
  for (const int rate : {44100, 48000, 88200, 96000}) {
    rateBox_.addItem(juce::String(rate) + " Hz", id++);
  }
  rateBox_.setSelectedId(2, juce::dontSendNotification);  // 48000 Hz

  id = 1;
  for (const int size : {64, 128, 256, 512, 1024}) {
    bufferBox_.addItem(juce::String(size) + " frames", id++);
  }
  bufferBox_.setSelectedId(3, juce::dontSendNotification);  // 256 frames

  apiBox_.onChange = [this] {
    rebuildDeviceBox({});
    submitOutput();
  };
  deviceBox_.onChange = [this] { submitOutput(); };
  rateBox_.onChange = [this] { submitOutput(); };
  bufferBox_.onChange = [this] { submitOutput(); };

  toneButton_.setColour(juce::ToggleButton::textColourId, theme_.text);
  toneButton_.setColour(juce::ToggleButton::tickColourId, theme_.accent);
  toneButton_.onClick = [this] { submitTone(); };
  addAndMakeVisible(toneButton_);

  levelSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
  levelSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
  levelSlider_.setRange(kSliderMinDb, kSliderMaxDb, 1.0);
  levelSlider_.setValue(kSliderDefaultDb, juce::dontSendNotification);
  levelSlider_.setTextValueSuffix(" dB");
  levelSlider_.setColour(juce::Slider::thumbColourId, theme_.accent);
  levelSlider_.setColour(juce::Slider::trackColourId, theme_.accent.withAlpha(0.6F));
  levelSlider_.setColour(juce::Slider::textBoxTextColourId, theme_.text);
  levelSlider_.setColour(juce::Slider::textBoxOutlineColourId, theme_.textDim.withAlpha(0.4F));
  levelSlider_.onValueChange = [this] { submitTone(); };
  addAndMakeVisible(levelSlider_);

  warningLabel_.setText("Turn your speakers down first: the tone is a plain sine and is louder than it looks.",
                        juce::dontSendNotification);
  styleLabel(warningLabel_, theme_, theme_.textDim);
  addAndMakeVisible(warningLabel_);

  statsLabel_.setFont(juce::FontOptions{juce::Font::getDefaultMonospacedFontName(), 14.0F, juce::Font::plain});
  statsLabel_.setJustificationType(juce::Justification::topLeft);
  styleLabel(statsLabel_, theme_, theme_.text);
  addAndMakeVisible(statsLabel_);

  errorLabel_.setJustificationType(juce::Justification::topLeft);
  styleLabel(errorLabel_, theme_, juce::Colour{0xffff6b6b});
  addAndMakeVisible(errorLabel_);

  startTimerHz(kStatsRefreshHz);
  timerCallback();
}

AudioSettingsPanel::~AudioSettingsPanel() {
  stopTimer();
}

void AudioSettingsPanel::setDevices(const std::vector<core::AudioDeviceInfo>& devices) {
  outputs_.clear();
  for (const core::AudioDeviceInfo& device : devices) {
    if (device.isOutput) {
      outputs_.push_back(device);
    }
  }

  // Selecting items below must not fire onChange (that would re-open the device just to show the list).
  const core::AudioEngineStats current = stats_.stats();
  apiBox_.clear(juce::dontSendNotification);
  int apiId = 0;
  int selectedApi = 0;
  std::vector<std::string> seen;
  for (const core::AudioDeviceInfo& device : outputs_) {
    if (std::find(seen.begin(), seen.end(), device.apiName) != seen.end()) {
      continue;
    }
    seen.push_back(device.apiName);
    apiBox_.addItem(juce::String::fromUTF8(device.apiName.c_str()), ++apiId);
    if (device.apiName == current.apiName) {
      selectedApi = apiId;
    }
  }
  apiBox_.setSelectedId(selectedApi != 0 ? selectedApi : (apiId > 0 ? 1 : 0), juce::dontSendNotification);
  rebuildDeviceBox(juce::String::fromUTF8(current.deviceName.c_str()));
}

void AudioSettingsPanel::rebuildDeviceBox(const juce::String& preferredDevice) {
  deviceBox_.clear(juce::dontSendNotification);
  deviceBox_.addItem("System default", kSystemDefaultItemId);
  int selected = kSystemDefaultItemId;
  int id = kSystemDefaultItemId;
  for (const core::AudioDeviceInfo& device : outputs_) {
    if (juce::String::fromUTF8(device.apiName.c_str()) != apiBox_.getText()) {
      continue;
    }
    const juce::String name = juce::String::fromUTF8(device.name.c_str());
    deviceBox_.addItem(name, ++id);
    if (preferredDevice.isNotEmpty() && name == preferredDevice) {
      selected = id;
    }
  }
  deviceBox_.setSelectedId(selected, juce::dontSendNotification);
}

void AudioSettingsPanel::submitOutput() {
  const bool useDefault = deviceBox_.getSelectedId() == kSystemDefaultItemId || deviceBox_.getSelectedId() == 0;
  core::SetAudioOutput command;
  command.apiName = apiBox_.getText().toStdString();
  command.deviceName = useDefault ? std::string{} : deviceBox_.getText().toStdString();
  command.sampleRate = rateBox_.getText().getDoubleValue();  // "48000 Hz" parses as 48000
  command.bufferSize = bufferBox_.getText().getIntValue();   // "256 frames" parses as 256
  showError(bus_.submit(command, kOrigin));
}

void AudioSettingsPanel::submitTone() {
  const core::SetTestTone command{toneButton_.getToggleState(), kToneFrequencyHz,
                                  static_cast<float>(levelSlider_.getValue())};
  showError(bus_.submit(command, kOrigin));
}

void AudioSettingsPanel::showError(const std::optional<core::CommandError>& error) {
  errorLabel_.setText(error ? juce::String::fromUTF8(error->message.c_str()) : juce::String(),
                      juce::dontSendNotification);
}

void AudioSettingsPanel::timerCallback() {
  statsLabel_.setText(juce::String::fromUTF8(core::formatEngineStats(stats_.stats()).c_str()),
                      juce::dontSendNotification);
}

void AudioSettingsPanel::resized() {
  auto area = getLocalBounds().reduced(16);
  constexpr int kRow = 30;
  constexpr int kLabelWidth = 130;

  const auto row = [&](juce::Label& label, juce::Component& control) {
    auto line = area.removeFromTop(kRow);
    label.setBounds(line.removeFromLeft(kLabelWidth));
    control.setBounds(line.removeFromLeft(380));
    area.removeFromTop(6);
  };
  row(apiLabel_, apiBox_);
  row(deviceLabel_, deviceBox_);
  row(rateLabel_, rateBox_);
  row(bufferLabel_, bufferBox_);

  area.removeFromTop(10);
  auto toneRow = area.removeFromTop(kRow);
  toneButton_.setBounds(toneRow.removeFromLeft(kLabelWidth + 60));
  levelSlider_.setBounds(toneRow.removeFromLeft(320));
  warningLabel_.setBounds(area.removeFromTop(24));

  area.removeFromTop(10);
  statsLabel_.setBounds(area.removeFromTop(150));
  errorLabel_.setBounds(area.removeFromTop(48));
}

}  // namespace zyron::ui
