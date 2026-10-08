// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Decoder/JuceAudioFileDecoder.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <string>

namespace zyron::audio {

namespace {

constexpr int kReadChunkFrames = 65536;
constexpr int kOutputChannels = 2;

void setError(std::string* out, std::string message) {
  if (out != nullptr) {
    *out = std::move(message);
  }
}

}  // namespace

std::shared_ptr<TrackBuffer> decodeWithJuce(const std::filesystem::path& path, std::string* errorOut) {
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();

  const auto utf8Path = path.u8string();  // portable way to hand a Unicode path to JUCE
  const juce::File file(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8Path.data()),
                                               static_cast<int>(utf8Path.size())));
  if (!file.existsAsFile()) {
    setError(errorOut, "File not found: " + path.generic_string());
    return nullptr;
  }

  const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
  if (reader == nullptr) {
    setError(errorOut, "Unsupported or unreadable audio format: " + path.extension().string());
    return nullptr;
  }

  const double sampleRate = reader->sampleRate;
  const auto totalFrames = static_cast<std::int64_t>(reader->lengthInSamples);
  if (sampleRate <= 0.0 || totalFrames <= 0 || reader->numChannels == 0) {
    setError(errorOut, "The file contains no audio: " + path.generic_string());
    return nullptr;
  }
  if (static_cast<double>(totalFrames) / sampleRate > kMaxDecodedSeconds) {
    setError(errorOut, "The track is too long to load into memory: " + path.generic_string());
    return nullptr;
  }

  auto buffer = std::make_shared<TrackBuffer>(kOutputChannels, totalFrames, sampleRate);
  float* const left = buffer->channelData(0);
  float* const right = buffer->channelData(1);
  const bool mono = reader->numChannels == 1;

  juce::AudioBuffer<float> chunk(kOutputChannels, kReadChunkFrames);
  for (std::int64_t position = 0; position < totalFrames; position += kReadChunkFrames) {
    const int frames = static_cast<int>(std::min<std::int64_t>(kReadChunkFrames, totalFrames - position));
    if (!reader->read(&chunk, 0, frames, position, true, true)) {
      setError(errorOut, "Decoding failed at " + std::to_string(position / static_cast<std::int64_t>(sampleRate)) +
                             " s: " + path.generic_string());
      return nullptr;
    }
    const float* src0 = chunk.getReadPointer(0);
    const float* src1 = mono ? src0 : chunk.getReadPointer(1);
    std::copy_n(src0, frames, left + position);
    std::copy_n(src1, frames, right + position);
  }
  return buffer;
}

}  // namespace zyron::audio
