// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

#include "Core/AI/TransitionTypes.hpp"

namespace zyron::ui {

/// The DJ-facing (translated) name of a transition style, shared by the queue column, its menu and the settings panel.
[[nodiscard]] juce::String transitionStyleLabel(core::TransitionStyle style);

}  // namespace zyron::ui
