// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "Core/Audio/DeckTelemetry.hpp"
#include "Core/Audio/WaveformData.hpp"
#include "UI/Theme.hpp"

namespace zyron::ui {

/// Comprehensive waveform visualizer supporting full overview and scrolling detail waveform (SPEC section 24).
///
/// Features:
///  - Cached full overview waveform rendering with 3-band frequency coloring (Bass, Mid, Treble)
///  - Real-time scrolling detail waveform centered on stationary playhead
///  - Beatgrid overlay with downbeat (bar) markers and regular beat lines
///  - 8 Hot cue markers with custom colors, types, and labels (SPEC section 25)
///  - Active loop region highlighting and boundary markers (SPEC section 26)
///  - Interactive playhead seeking and scrubbing via mouse click and drag
class WaveformView : public juce::Component {
 public:
  enum class Mode {
    Both,
    OverviewOnly,
    DetailOnly
  };

  explicit WaveformView(Theme theme = Theme::dark());
  ~WaveformView() override = default;

  void setTheme(const Theme& theme);
  void setMode(Mode mode);
  [[nodiscard]] Mode mode() const noexcept { return mode_; }
  void setWaveformData(core::WaveformData data);
  void updateTelemetry(const core::DeckTelemetry& telemetry);
  void setZoomSeconds(double zoomSec);

  // User interaction callbacks
  std::function<void(double seekSeconds)> onSeekRequested;
  std::function<void(int cueIndex)> onCueTriggered;

  void paint(juce::Graphics& g) override;
  void resized() override;

  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

 private:
  void renderOverviewCache();
  void paintOverview(juce::Graphics& g, juce::Rectangle<int> bounds);
  void paintDetail(juce::Graphics& g, juce::Rectangle<int> bounds);

  [[nodiscard]] juce::Colour calculateBandColour(float low, float mid, float high) const;

  Theme theme_;
  Mode mode_{Mode::Both};
  core::WaveformData waveformData_;
  core::DeckTelemetry telemetry_;

  double zoomSeconds_{8.0};  // Total visible seconds in detail window (e.g. 8.0s)
  juce::Image overviewCacheImage_;
  bool overviewDirty_{true};

  juce::Rectangle<int> overviewBounds_;
  juce::Rectangle<int> detailBounds_;

  bool isDraggingOverview_{false};
  bool isDraggingDetail_{false};
  int lastDragX_{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};

}  // namespace zyron::ui
