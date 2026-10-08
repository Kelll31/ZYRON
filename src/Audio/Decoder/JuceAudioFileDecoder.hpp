// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Audio/Deck/TrackBuffer.hpp"

namespace zyron::audio {

/// Decodes MP3, FLAC, Ogg Vorbis, AIFF and WAV variants through JUCE's built-in codecs (ADR-0014). JUCE stays an
/// implementation detail of the .cpp: the interface is JUCE-free so it plugs into TrackLoader::setFallbackDecoder.
///
/// The whole track is decoded into memory as 32-bit float, 2 channels (mono is duplicated, extra channels dropped).
/// Runs on a loader thread, never on the audio thread. Returns nullptr (and fills `errorOut`) on failure.
[[nodiscard]] std::shared_ptr<TrackBuffer> decodeWithJuce(const std::filesystem::path& path,
                                                          std::string* errorOut = nullptr);

/// Longest track the decoder accepts, to bound memory (about 2 GB of float at 44.1 kHz stereo would be far more).
inline constexpr double kMaxDecodedSeconds = 3.0 * 3600.0;

}  // namespace zyron::audio
