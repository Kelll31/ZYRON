// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Core/System/HardwareProbe.hpp"

namespace zyron::midi {

/// Lists the MIDI input and output ports currently present. Opens nothing.
class JuceMidiProbe final : public core::MidiProbe {
 public:
  [[nodiscard]] core::MidiDevices probe() override;
};

}  // namespace zyron::midi
