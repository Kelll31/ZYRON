// SPDX-License-Identifier: AGPL-3.0-only
#include "UI/Fx/FxModel.hpp"

namespace zyron::ui {

juce::String fxTypeLabel(core::FxType type) {
  switch (type) {
    case core::FxType::None: return TRANS("None");
    case core::FxType::Echo: return TRANS("Echo");
    case core::FxType::Reverb: return TRANS("Reverb");
    case core::FxType::Flanger: return TRANS("Flanger");
    case core::FxType::Phaser: return TRANS("Phaser");
    case core::FxType::Delay: return TRANS("Delay");
  }
  return {};
}

juce::String fxParamLabel(core::FxType type) {
  switch (type) {
    case core::FxType::Echo:
    case core::FxType::Delay: return TRANS("FEEDBACK");
    case core::FxType::Reverb: return TRANS("ROOM");
    case core::FxType::Flanger:
    case core::FxType::Phaser: return TRANS("RATE");
    case core::FxType::None: break;
  }
  return TRANS("PARAM");
}

juce::String fxHitLabel(core::FxHitType type) {
  switch (type) {
    case core::FxHitType::AirHorn: return TRANS("Air horn");
    case core::FxHitType::Siren: return TRANS("Siren");
    case core::FxHitType::Riser: return TRANS("Riser");
    case core::FxHitType::Downlifter: return TRANS("Downlifter");
    case core::FxHitType::Impact: return TRANS("Impact");
    case core::FxHitType::Laser: return TRANS("Laser");
  }
  return {};
}

}  // namespace zyron::ui
