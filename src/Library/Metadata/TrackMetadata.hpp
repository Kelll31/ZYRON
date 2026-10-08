// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

namespace zyron::library {

/// Metadata extracted from an audio file (SPEC sections 27, 29, 31).
struct TrackMetadata {
  std::string title;
  std::string artist;
  std::string album;
  std::string genre;
  int year{0};
  double durationSec{0.0};
  int sampleRate{44100};
  int channels{2};
  int bitDepth{16};
  int bitrateKbps{0};
};

}  // namespace zyron::library
