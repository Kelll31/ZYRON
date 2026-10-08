// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Waveform/WaveformView.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace zyron::ui {

WaveformView::WaveformView(Theme theme)
    : theme_(theme) {}

void WaveformView::setTheme(const Theme& theme) {
  theme_ = theme;
  overviewDirty_ = true;
  repaint();
}

void WaveformView::setMode(Mode mode) {
  if (mode_ != mode) {
    mode_ = mode;
    resized();
    repaint();
  }
}

void WaveformView::setWaveformData(core::WaveformData data) {
  waveformData_ = std::move(data);
  overviewDirty_ = true;
  repaint();
}

void WaveformView::updateTelemetry(const core::DeckTelemetry& telemetry) {
  telemetry_ = telemetry;
  repaint();
}

void WaveformView::setZoomSeconds(double zoomSec) {
  zoomSeconds_ = std::clamp(zoomSec, 1.0, 60.0);
  repaint();
}

namespace {

void fillTriangle(juce::Graphics& g, float x1, float y1, float x2, float y2, float x3, float y3) {
  juce::Path p;
  p.addTriangle(x1, y1, x2, y2, x3, y3);
  g.fillPath(p);
}

}  // namespace

void WaveformView::resized() {
  auto b = getLocalBounds();
  if (mode_ == Mode::OverviewOnly) {
    overviewBounds_ = b;
    detailBounds_ = {};
  } else if (mode_ == Mode::DetailOnly) {
    overviewBounds_ = {};
    detailBounds_ = b;
  } else {
    // Upper 25% for overview (min 30px, max 45px), rest for detail waveform
    const int overviewHeight = std::clamp(b.getHeight() / 4, 30, 45);
    overviewBounds_ = b.removeFromTop(overviewHeight);
    detailBounds_ = b;
  }
  overviewDirty_ = true;
}

juce::Colour WaveformView::calculateBandColour(float low, float mid, float high) const {
  const float total = low + mid + high;
  if (total < 1e-4f) {
    return theme_.waveformMid;
  }
  const float wL = low / total;
  const float wM = mid / total;
  const float wH = high / total;

  const float r = wL * static_cast<float>(theme_.waveformLow.getFloatRed()) +
                  wM * static_cast<float>(theme_.waveformMid.getFloatRed()) +
                  wH * static_cast<float>(theme_.waveformHigh.getFloatRed());
  const float g = wL * static_cast<float>(theme_.waveformLow.getFloatGreen()) +
                  wM * static_cast<float>(theme_.waveformMid.getFloatGreen()) +
                  wH * static_cast<float>(theme_.waveformHigh.getFloatGreen());
  const float b = wL * static_cast<float>(theme_.waveformLow.getFloatBlue()) +
                  wM * static_cast<float>(theme_.waveformMid.getFloatBlue()) +
                  wH * static_cast<float>(theme_.waveformHigh.getFloatBlue());

  return juce::Colour::fromFloatRGBA(std::clamp(r, 0.0f, 1.0f),
                                     std::clamp(g, 0.0f, 1.0f),
                                     std::clamp(b, 0.0f, 1.0f),
                                     1.0f);
}

void WaveformView::renderOverviewCache() {
  const int w = overviewBounds_.getWidth();
  const int h = overviewBounds_.getHeight();

  if (w <= 0 || h <= 0) {
    overviewDirty_ = false;
    return;
  }

  overviewCacheImage_ = juce::Image(juce::Image::ARGB, w, h, true);
  juce::Graphics g(overviewCacheImage_);

  g.fillAll(theme_.waveformBackground);

  const auto& frames = !waveformData_.overview.empty() ? waveformData_.overview : waveformData_.detail;
  if (frames.empty()) {
    // Draw empty center guide line
    g.setColour(theme_.panel);
    g.drawHorizontalLine(h / 2, 0.0f, static_cast<float>(w));
    overviewDirty_ = false;
    return;
  }

  const float midY = static_cast<float>(h) * 0.5f;
  const float halfH = midY * 0.95f;
  const float framesPerPixel = static_cast<float>(frames.size()) / static_cast<float>(w);

  for (int x = 0; x < w; ++x) {
    const auto startIdx = static_cast<std::size_t>(static_cast<float>(x) * framesPerPixel);
    const auto endIdx = std::min(frames.size(),
                                 static_cast<std::size_t>(static_cast<float>(x + 1) * framesPerPixel) + 1);

    float maxAmp = 0.0f;
    float sumLow = 0.0f, sumMid = 0.0f, sumHigh = 0.0f;
    std::size_t count = 0;

    for (std::size_t i = startIdx; i < endIdx && i < frames.size(); ++i) {
      const auto& f = frames[i];
      maxAmp = std::max({maxAmp, std::abs(f.minLeft), std::abs(f.maxLeft),
                                 std::abs(f.minRight), std::abs(f.maxRight)});
      sumLow += f.lowEnergy;
      sumMid += f.midEnergy;
      sumHigh += f.highEnergy;
      ++count;
    }

    if (count > 0) {
      const float inv = 1.0f / static_cast<float>(count);
      const auto col = calculateBandColour(sumLow * inv, sumMid * inv, sumHigh * inv);
      const float peakY = std::clamp(maxAmp * halfH, 1.0f, halfH);

      g.setColour(col);
      g.drawVerticalLine(x, midY - peakY, midY + peakY);
    }
  }

  // Draw subtle center line
  g.setColour(juce::Colours::white.withAlpha(0.15f));
  g.drawHorizontalLine(h / 2, 0.0f, static_cast<float>(w));

  overviewDirty_ = false;
}

void WaveformView::paint(juce::Graphics& g) {
  g.fillAll(theme_.background);

  if (mode_ != Mode::DetailOnly && !overviewBounds_.isEmpty()) {
    paintOverview(g, overviewBounds_);
  }
  if (mode_ != Mode::OverviewOnly && !detailBounds_.isEmpty()) {
    paintDetail(g, detailBounds_);
  }

  // Border between overview and detail
  if (mode_ == Mode::Both && !overviewBounds_.isEmpty() && !detailBounds_.isEmpty()) {
    g.setColour(theme_.panel);
    g.drawHorizontalLine(overviewBounds_.getBottom(), 0.0f, static_cast<float>(getWidth()));
  }
}

void WaveformView::paintOverview(juce::Graphics& g, juce::Rectangle<int> bounds) {
  if (bounds.isEmpty()) return;

  if (overviewDirty_ || overviewCacheImage_.isNull() ||
      overviewCacheImage_.getWidth() != bounds.getWidth() ||
      overviewCacheImage_.getHeight() != bounds.getHeight()) {
    renderOverviewCache();
  }

  if (overviewCacheImage_.isValid()) {
    g.drawImageAt(overviewCacheImage_, bounds.getX(), bounds.getY());
  }

  const double duration = (telemetry_.durationSec > 0.0)
                              ? telemetry_.durationSec
                              : waveformData_.durationSec();
  if (duration <= 0.0) return;

  const float w = static_cast<float>(bounds.getWidth());
  const float h = static_cast<float>(bounds.getHeight());
  const float topY = static_cast<float>(bounds.getY());

  // 1. Highlight active loop region (SPEC section 26)
  if (telemetry_.loop.active && telemetry_.loop.endTimeSec > telemetry_.loop.startTimeSec) {
    const float x1 = static_cast<float>(telemetry_.loop.startTimeSec / duration) * w;
    const float x2 = static_cast<float>(telemetry_.loop.endTimeSec / duration) * w;
    const auto loopRect = juce::Rectangle<float>(x1, topY, std::max(2.0f, x2 - x1), h);

    g.setColour(theme_.loopRegion);
    g.fillRect(loopRect);
    g.setColour(theme_.accent);
    g.drawRect(loopRect, 1.0f);
  }

  // 2. Draw Hot Cues on overview (SPEC section 25)
  for (const auto& cueOpt : telemetry_.hotCues) {
    if (!cueOpt.has_value()) continue;
    const auto& cue = *cueOpt;
    const float cueX = static_cast<float>(cue.timeSec / duration) * w;

    juce::Colour cueColor = theme_.accent;
    if (!cue.color.empty()) {
      cueColor = juce::Colour::fromString(cue.color);
    }

    g.setColour(cueColor);
    g.drawVerticalLine(static_cast<int>(cueX), topY, topY + h);

    // Marker flag at top
    fillTriangle(g, cueX, topY, cueX + 5.0f, topY, cueX, topY + 6.0f);
  }

  // 3. Overview Playhead needle
  const float playX = static_cast<float>(telemetry_.currentTimeSec / duration) * w;
  g.setColour(theme_.waveformPlayhead);
  g.drawVerticalLine(static_cast<int>(playX), topY, topY + h);
  g.fillEllipse(playX - 2.5f, topY + h - 5.0f, 5.0f, 5.0f);
}

void WaveformView::paintDetail(juce::Graphics& g, juce::Rectangle<int> bounds) {
  if (bounds.isEmpty()) return;

  g.fillAll(theme_.waveformBackground);

  const int w = bounds.getWidth();
  const int h = bounds.getHeight();
  const float topY = static_cast<float>(bounds.getY());
  const float midY = topY + static_cast<float>(h) * 0.5f;
  const float halfH = static_cast<float>(h) * 0.45f;
  const float centerX = static_cast<float>(w) * 0.5f;

  const double pps = static_cast<double>(w) / zoomSeconds_;  // pixels per second
  const double currentSec = telemetry_.currentTimeSec;
  const double windowStartSec = currentSec - (centerX / pps);
  const double windowEndSec = currentSec + ((static_cast<double>(w) - centerX) / pps);

  // 1. Draw Active Loop Region (SPEC section 26)
  if (telemetry_.loop.active && telemetry_.loop.endTimeSec > telemetry_.loop.startTimeSec) {
    const auto loopStartSec = telemetry_.loop.startTimeSec;
    const auto loopEndSec = telemetry_.loop.endTimeSec;

    if (loopEndSec >= windowStartSec && loopStartSec <= windowEndSec) {
      const float x1 = centerX + static_cast<float>((loopStartSec - currentSec) * pps);
      const float x2 = centerX + static_cast<float>((loopEndSec - currentSec) * pps);
      const auto loopRect = juce::Rectangle<float>(x1, topY, x2 - x1, static_cast<float>(h));

      g.setColour(theme_.loopRegion);
      g.fillRect(loopRect);
      g.setColour(theme_.accent);
      g.drawRect(loopRect, 1.5f);

      g.setFont(10.0f);
      g.drawText("LOOP", loopRect.reduced(4.0f), juce::Justification::topLeft, false);
    }
  }

  // 2. Draw Beatgrid Lines & Downbeats (SPEC sections 15, 24)
  if (telemetry_.beatgrid.bpm > 10.0 && telemetry_.beatgrid.beatIntervalSec > 0.001) {
    const double interval = telemetry_.beatgrid.beatIntervalSec;
    const double firstBeat = telemetry_.beatgrid.firstBeatTimeSec;

    const auto minBeatIdx = static_cast<std::int64_t>(std::floor((windowStartSec - firstBeat) / interval));
    const auto maxBeatIdx = static_cast<std::int64_t>(std::ceil((windowEndSec - firstBeat) / interval));

    for (std::int64_t b = minBeatIdx; b <= maxBeatIdx; ++b) {
      const double beatTime = firstBeat + static_cast<double>(b) * interval;
      const float bx = centerX + static_cast<float>((beatTime - currentSec) * pps);

      if (bx < 0.0f || bx > static_cast<float>(w)) continue;

      const bool isDownbeat = (b % 4 == 0);
      if (isDownbeat) {
        g.setColour(theme_.beatgridDownbeat);
        g.drawVerticalLine(static_cast<int>(bx), topY, topY + static_cast<float>(h));

        // Draw bar number
        const std::int64_t barNum = (b / 4) + 1;
        g.setColour(theme_.text);
        g.setFont(9.0f);
        g.drawText(juce::String(barNum), static_cast<int>(bx) + 2, static_cast<int>(topY) + 2, 28, 12,
                   juce::Justification::left, false);
      } else {
        g.setColour(theme_.beatgridLine);
        g.drawVerticalLine(static_cast<int>(bx), topY + 4.0f, topY + static_cast<float>(h) - 4.0f);
      }
    }
  }

  // 3. Draw Scrolling Detail Waveform Peaks
  const auto& detail = waveformData_.detail;
  if (!detail.empty() && waveformData_.sampleRate > 0 && waveformData_.samplesPerFrame > 0) {
    const double secPerFrame = static_cast<double>(waveformData_.samplesPerFrame) /
                               static_cast<double>(waveformData_.sampleRate);

    for (int x = 0; x < w; ++x) {
      const double t = currentSec + (static_cast<double>(x) - centerX) / pps;
      if (t < 0.0) continue;

      const auto frameIdx = static_cast<std::size_t>(t / secPerFrame);
      if (frameIdx >= detail.size()) continue;

      const auto& f = detail[frameIdx];
      const auto col = calculateBandColour(f.lowEnergy, f.midEnergy, f.highEnergy);

      // Stereo peaks: Left channel top, Right channel bottom
      const float leftAmp = std::clamp(std::max(std::abs(f.minLeft), std::abs(f.maxLeft)) * halfH, 1.0f, halfH);
      const float rightAmp = std::clamp(std::max(std::abs(f.minRight), std::abs(f.maxRight)) * halfH, 1.0f, halfH);

      g.setColour(col);
      g.drawVerticalLine(x, midY - leftAmp, midY + rightAmp);
    }
  }

  // 4. Draw Hot Cue Flags (SPEC section 25)
  for (const auto& cueOpt : telemetry_.hotCues) {
    if (!cueOpt.has_value()) continue;
    const auto& cue = *cueOpt;

    if (cue.timeSec >= windowStartSec && cue.timeSec <= windowEndSec) {
      const float cx = centerX + static_cast<float>((cue.timeSec - currentSec) * pps);
      juce::Colour cueColor = theme_.accent;
      if (!cue.color.empty()) {
        cueColor = juce::Colour::fromString(cue.color);
      }

      g.setColour(cueColor);
      g.drawVerticalLine(static_cast<int>(cx), topY, topY + static_cast<float>(h));

      // Flag badge
      const juce::Rectangle<float> badgeRect(cx, topY + 2.0f, 44.0f, 16.0f);
      g.fillRoundedRectangle(badgeRect, 2.0f);
      g.setColour(juce::Colours::black);
      g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
      juce::String label = juce::String(cue.index);
      if (!cue.name.empty()) {
        label << " " << cue.name;
      }
      g.drawText(label, badgeRect.toNearestInt(), juce::Justification::centred, true);
    }
  }

  // Center division line
  g.setColour(juce::Colours::white.withAlpha(0.12f));
  g.drawHorizontalLine(static_cast<int>(midY), 0.0f, static_cast<float>(w));

  // 5. Stationary Playhead Needle in Center
  g.setColour(theme_.waveformPlayhead);
  g.drawVerticalLine(static_cast<int>(centerX), topY, topY + static_cast<float>(h));

  // Top and bottom needle triangles
  fillTriangle(g, centerX - 4.0f, topY, centerX + 4.0f, topY, centerX, topY + 6.0f);
  fillTriangle(g, centerX - 4.0f, topY + static_cast<float>(h),
               centerX + 4.0f, topY + static_cast<float>(h),
               centerX, topY + static_cast<float>(h) - 6.0f);
}

void WaveformView::mouseDown(const juce::MouseEvent& e) {
  if (overviewBounds_.contains(e.getPosition())) {
    isDraggingOverview_ = true;
    isDraggingDetail_ = false;

    const double duration = (telemetry_.durationSec > 0.0)
                                ? telemetry_.durationSec
                                : waveformData_.durationSec();
    if (duration > 0.0 && overviewBounds_.getWidth() > 0) {
      const double ratio = static_cast<double>(e.x - overviewBounds_.getX()) /
                           static_cast<double>(overviewBounds_.getWidth());
      const double targetSec = std::clamp(ratio * duration, 0.0, duration);
      if (onSeekRequested) {
        onSeekRequested(targetSec);
      }
    }
  } else if (detailBounds_.contains(e.getPosition())) {
    isDraggingDetail_ = true;
    isDraggingOverview_ = false;
    lastDragX_ = e.x;
  }
}

void WaveformView::mouseDrag(const juce::MouseEvent& e) {
  if (isDraggingOverview_) {
    const double duration = (telemetry_.durationSec > 0.0)
                                ? telemetry_.durationSec
                                : waveformData_.durationSec();
    if (duration > 0.0 && overviewBounds_.getWidth() > 0) {
      const double ratio = static_cast<double>(e.x - overviewBounds_.getX()) /
                           static_cast<double>(overviewBounds_.getWidth());
      const double targetSec = std::clamp(ratio * duration, 0.0, duration);
      if (onSeekRequested) {
        onSeekRequested(targetSec);
      }
    }
  } else if (isDraggingDetail_) {
    const int deltaX = e.x - lastDragX_;
    lastDragX_ = e.x;

    const double pps = static_cast<double>(detailBounds_.getWidth()) / zoomSeconds_;
    if (pps > 0.001) {
      const double deltaSec = -static_cast<double>(deltaX) / pps;
      const double duration = (telemetry_.durationSec > 0.0)
                                  ? telemetry_.durationSec
                                  : waveformData_.durationSec();
      const double targetSec = std::clamp(telemetry_.currentTimeSec + deltaSec, 0.0,
                                          duration > 0.0 ? duration : 3600.0);
      if (onSeekRequested) {
        onSeekRequested(targetSec);
      }
    }
  }
}

void WaveformView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
  // Scrub on horizontal or vertical wheel
  const double scrollAmount = (std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) ? wheel.deltaX : wheel.deltaY;
  if (std::abs(scrollAmount) > 0.001) {
    const double deltaSec = -scrollAmount * (zoomSeconds_ * 0.2);
    const double duration = (telemetry_.durationSec > 0.0)
                                ? telemetry_.durationSec
                                : waveformData_.durationSec();
    const double targetSec = std::clamp(telemetry_.currentTimeSec + deltaSec, 0.0,
                                        duration > 0.0 ? duration : 3600.0);
    if (onSeekRequested) {
      onSeekRequested(targetSec);
    }
  }
}

}  // namespace zyron::ui
