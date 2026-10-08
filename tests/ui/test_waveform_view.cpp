// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Audio/DeckTelemetry.hpp"
#include "Core/Audio/WaveformData.hpp"
#include "UI/Theme.hpp"
#include "UI/Waveform/WaveformView.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron;

TEST_CASE("WaveformView headless rendering and interaction", "[ui][waveform]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  ui::WaveformView view(ui::Theme::dark());
  view.setSize(600, 160);

  SECTION("Renders cleanly when empty") {
    juce::Image target(juce::Image::ARGB, 600, 160, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(view.paintEntireComponent(g, true));
  }

  SECTION("Renders with populated waveform data, cues, and active loop") {
    core::WaveformData data;
    data.sampleRate = 44100;
    data.channels = 2;
    data.samplesPerFrame = 256;

    // Create 1000 frames (approx 5.8s)
    for (int i = 0; i < 1000; ++i) {
      core::WaveformPoint p;
      p.minLeft = -0.7f;
      p.maxLeft = 0.7f;
      p.minRight = -0.6f;
      p.maxRight = 0.6f;
      p.lowEnergy = 0.8f;
      p.midEnergy = 0.4f;
      p.highEnergy = 0.2f;
      data.detail.push_back(p);
      if (i % 8 == 0) {
        data.overview.push_back(p);
      }
    }

    view.setWaveformData(std::move(data));

    core::DeckTelemetry telem;
    telem.deck = core::DeckId::A;
    telem.hasTrack = true;
    telem.isPlaying = true;
    telem.currentTimeSec = 2.5;
    telem.durationSec = 5.8;

    // Set beatgrid (174 BPM DnB)
    telem.beatgrid.bpm = 174.0;
    telem.beatgrid.beatIntervalSec = 60.0 / 174.0;
    telem.beatgrid.firstBeatTimeSec = 0.2;

    // Set Loop (2 beats from 1.0s to 1.689s)
    telem.loop.active = true;
    telem.loop.startTimeSec = 1.0;
    telem.loop.endTimeSec = 1.689;

    // Set hot cues
    core::CuePointTelemetry cue1;
    cue1.index = 1;
    cue1.timeSec = 0.2;
    cue1.name = "Intro";
    cue1.color = "0xff3ddc97";
    telem.hotCues[0] = cue1;

    core::CuePointTelemetry cue2;
    cue2.index = 2;
    cue2.timeSec = 2.0;
    cue2.name = "Drop";
    cue2.color = "0xffff3b30";
    telem.hotCues[1] = cue2;

    view.updateTelemetry(telem);

    // Paint into target image
    juce::Image target(juce::Image::ARGB, 600, 160, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(view.paintEntireComponent(g, true));

    // Change theme and zoom
    view.setTheme(ui::Theme::light());
    view.setZoomSeconds(12.0);
    REQUIRE_NOTHROW(view.paintEntireComponent(g, true));
  }

  SECTION("Overview mouse click invokes seek callback") {
    core::WaveformData data;
    data.sampleRate = 44100;
    data.samplesPerFrame = 256;
    data.detail.resize(1000);
    view.setWaveformData(data);

    core::DeckTelemetry telem;
    telem.durationSec = 10.0;
    view.updateTelemetry(telem);

    double soughtSec = -1.0;
    view.onSeekRequested = [&](double s) { soughtSec = s; };

    // Overview is in top 30-45px. Click at x = 300 (middle) and y = 15
    const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(),
                                 juce::Point<float>(300.0f, 15.0f),
                                 juce::ModifierKeys::leftButtonModifier,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &view, &view,
                                 juce::Time::getCurrentTime(),
                                 juce::Point<float>(300.0f, 15.0f),
                                 juce::Time::getCurrentTime(),
                                 1, false);

    view.mouseDown(click);
    CHECK(soughtSec >= 4.5);
    CHECK(soughtSec <= 5.5);
  }
}
