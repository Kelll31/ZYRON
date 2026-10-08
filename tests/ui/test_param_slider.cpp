// SPDX-License-Identifier: AGPL-3.0-only
#include <juce_events/juce_events.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "UI/ParamSlider.hpp"

using zyron::ui::ParamSlider;
using Catch::Matchers::WithinAbs;

TEST_CASE("ParamSlider: reset to default and typed values", "[ui][param_slider]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  ParamSlider slider;
  slider.setRange(-24.0, 12.0, 0.1);
  slider.setDefaultValue(0.0);
  slider.setValue(7.5, juce::dontSendNotification);

  int notifications = 0;
  double lastValue = 99.0;
  slider.onValueChange = [&] {
    ++notifications;
    lastValue = slider.getValue();
  };

  SECTION("a reset (right-click) goes back to the default and tells the listeners") {
    slider.resetToDefault();
    CHECK_THAT(slider.getValue(), WithinAbs(0.0, 1e-9));
    CHECK(notifications == 1);
    CHECK_THAT(lastValue, WithinAbs(0.0, 1e-9));
  }

  SECTION("a default other than zero is restored (volume faders default to 1)") {
    slider.setRange(0.0, 1.0, 0.01);
    slider.setDefaultValue(1.0);
    slider.setValue(0.25, juce::dontSendNotification);
    slider.resetToDefault();
    CHECK_THAT(slider.getValue(), WithinAbs(1.0, 1e-9));
  }

  SECTION("typed values are applied, accepting a decimal comma") {
    CHECK(slider.setFromText("-3.5"));
    CHECK_THAT(slider.getValue(), WithinAbs(-3.5, 1e-6));
    CHECK(slider.setFromText(" 4,2 "));
    CHECK_THAT(slider.getValue(), WithinAbs(4.2, 1e-6));
    CHECK(notifications == 2);
  }

  SECTION("typed values outside the range are clamped") {
    CHECK(slider.setFromText("100"));
    CHECK_THAT(slider.getValue(), WithinAbs(12.0, 1e-9));
    CHECK(slider.setFromText("-500"));
    CHECK_THAT(slider.getValue(), WithinAbs(-24.0, 1e-9));
  }

  SECTION("text that is not a number changes nothing") {
    CHECK_FALSE(slider.setFromText("loud"));
    CHECK_FALSE(slider.setFromText(""));
    CHECK_THAT(slider.getValue(), WithinAbs(7.5, 1e-9));
    CHECK(notifications == 0);
  }
}
