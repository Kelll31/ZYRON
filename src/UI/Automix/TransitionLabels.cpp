// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Automix/TransitionLabels.hpp"

#include "UI/Localization.hpp"

namespace zyron::ui {

juce::String transitionStyleLabel(core::TransitionStyle style) {
  switch (style) {
    case core::TransitionStyle::BassSwap: return TRANS("Blend");
    case core::TransitionStyle::StemBlend: return TRANS("Stem blend");
    case core::TransitionStyle::QuickCut: return TRANS("Cut on the drop");
    case core::TransitionStyle::FilterFade: return TRANS("Filter sweep");
    case core::TransitionStyle::VolumeCrossfade: return TRANS("Crossfade");
    case core::TransitionStyle::LoopRoll: return TRANS("Loop roll");
    case core::TransitionStyle::Brake: return TRANS("Brake");
    case core::TransitionStyle::Scratch: return TRANS("Scratch");
    case core::TransitionStyle::BeatLoopIn: return TRANS("Beat loop");
    case core::TransitionStyle::DoubleDrop: return TRANS("Double drop");
    case core::TransitionStyle::EchoOut: return TRANS("Echo out");
    case core::TransitionStyle::ReverbOut: return TRANS("Reverb out");
  }
  return {};
}

}  // namespace zyron::ui
