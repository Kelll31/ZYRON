// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Core/System/HardwareProbe.hpp"

namespace zyron::platform {

/// OS name, CPU model/cores and installed RAM through juce::SystemStats - one implementation for all three
/// operating systems, so there is nothing OS-specific to maintain here (SPEC section 80).
class JuceSystemProbe final : public core::SystemProbe {
 public:
  [[nodiscard]] core::SystemInfo probe() override;
};

}  // namespace zyron::platform
