// SPDX-License-Identifier: AGPL-3.0-only
#include "Analysis/Chord/ChordAnalyzer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <vector>

#include "Analysis/Features/Resampler.hpp"

namespace zyron::analysis {

namespace {

constexpr const char* kRootNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

constexpr const char* kQualitySuffixes[14] = {
    "", "m", "7", "maj7", "m7", "dim", "aug", "sus4", "sus2", "6", "m6", "7sus4", "dim7", "m7b5"
};

constexpr ChordQuality kQualityEnums[14] = {
    ChordQuality::Major,
    ChordQuality::Minor,
    ChordQuality::Dominant7,
    ChordQuality::Major7,
    ChordQuality::Minor7,
    ChordQuality::Diminished,
    ChordQuality::Augmented,
    ChordQuality::Sus4,
    ChordQuality::Sus2,
    ChordQuality::Sixth,
    ChordQuality::MinorSixth,
    ChordQuality::Dominant7Sus4,
    ChordQuality::Diminished7,
    ChordQuality::HalfDiminished7
};

// 12-element pitch class template definitions for DSP fallback
struct ChordTemplate {
  int qualityIdx;
  std::vector<int> intervals;
};

const std::vector<ChordTemplate>& getTemplates() {
  static const std::vector<ChordTemplate> templates = {
      {0, {0, 4, 7}},          // Major
      {1, {0, 3, 7}},          // Minor
      {2, {0, 4, 7, 10}},      // Dominant 7
      {3, {0, 4, 7, 11}},      // Major 7
      {4, {0, 3, 7, 10}},      // Minor 7
      {5, {0, 3, 6}},          // Diminished
      {6, {0, 4, 8}},          // Augmented
      {7, {0, 5, 7}},          // Sus4
      {8, {0, 2, 7}},          // Sus2
      {9, {0, 4, 7, 9}},       // 6
      {10, {0, 3, 7, 9}},      // Minor 6
      {11, {0, 5, 7, 10}},     // Dominant 7 sus4
      {12, {0, 3, 6, 9}},      // Diminished 7
      {13, {0, 3, 6, 10}}      // Half-diminished
  };
  return templates;
}

}  // namespace

float HarmonyAnalysisResult::progressionCompatibility(
    const HarmonyAnalysisResult& outgoing,
    const HarmonyAnalysisResult& incoming) noexcept {
  const float keyScore = MusicalKey::compatibilityScore(outgoing.estimatedKey, incoming.estimatedKey);

  float chromaSim = 0.5f;
  if (outgoing.chromaProfile.size() == 12 && incoming.chromaProfile.size() == 12) {
    double dot = 0.0;
    double normOut = 0.0;
    double normIn = 0.0;
    for (std::size_t i = 0; i < 12; ++i) {
      dot += static_cast<double>(outgoing.chromaProfile[i] * incoming.chromaProfile[i]);
      normOut += static_cast<double>(outgoing.chromaProfile[i] * outgoing.chromaProfile[i]);
      normIn += static_cast<double>(incoming.chromaProfile[i] * incoming.chromaProfile[i]);
    }
    if (normOut > 1e-9 && normIn > 1e-9) {
      chromaSim = static_cast<float>(dot / (std::sqrt(normOut) * std::sqrt(normIn)));
    }
  }

  return std::clamp(0.60f * keyScore + 0.40f * chromaSim, 0.0f, 1.0f);
}

ChordAnalyzer::ChordAnalyzer() : inferenceCallback_(nullptr) {}

ChordAnalyzer::ChordAnalyzer(InferenceCallback inferenceCallback)
    : inferenceCallback_(std::move(inferenceCallback)) {}

Chord ChordAnalyzer::classIndexToChord(int classIndex, float confidence) {
  if (classIndex <= 0 || classIndex > 168) {
    return {-1, ChordQuality::None, "N", confidence};
  }

  const int idx = classIndex - 1;
  const int qualityVal = idx / 12;
  const int rootIdx = idx % 12;

  if (qualityVal >= 14 || rootIdx >= 12) {
    return {-1, ChordQuality::None, "N", confidence};
  }

  Chord c;
  c.rootNote = rootIdx;
  c.quality = kQualityEnums[qualityVal];
  c.name = std::string(kRootNames[rootIdx]) + kQualitySuffixes[qualityVal];
  c.confidence = std::clamp(confidence, 0.0f, 1.0f);
  return c;
}

int ChordAnalyzer::chordToClassIndex(const Chord& chord) noexcept {
  if (chord.isSilent() || chord.rootNote < 0 || chord.rootNote >= 12) {
    return 0;
  }

  for (int q = 0; q < 14; ++q) {
    if (kQualityEnums[q] == chord.quality) {
      return 1 + q * 12 + (chord.rootNote % 12);
    }
  }
  return 0;
}

HarmonyAnalysisResult ChordAnalyzer::analyze(
    const float* audio, std::size_t numSamples, int sampleRate) const {
  HarmonyAnalysisResult result;
  if (!audio || numSamples == 0 || sampleRate <= 0) {
    return result;
  }

  // 1. Resample to 22 050 Hz if needed (ChordMini input specification)
  std::vector<float> audio22k;
  const float* pAudio = audio;
  std::size_t nSamples = numSamples;

  if (sampleRate != 22050) {
    audio22k = AudioResampler::resample(audio, numSamples, sampleRate, 22050);
    pAudio = audio22k.data();
    nSamples = audio22k.size();
  }

  if (nSamples < cqt_.config().hopLength) {
    return result;
  }

  // 2. Compute CQT Spectrogram [numFrames][144]
  const auto cqtFrames = cqt_.processMagnitude(pAudio, nSamples);
  const std::size_t numFrames = cqtFrames.size();
  if (numFrames == 0) {
    return result;
  }

  const double hopSec = static_cast<double>(cqt_.config().hopLength) / 22050.0;
  std::vector<Chord> frameChords(numFrames);

  // 3. Inference path (via ChordMini ONNX inference callback if provided)
  if (inferenceCallback_) {
    std::vector<float> cqtFlat(numFrames * 144);
    for (std::size_t t = 0; t < numFrames; ++t) {
      for (std::size_t b = 0; b < 144; ++b) {
        cqtFlat[t * 144 + b] = cqtFrames[t][b];
      }
    }

    const auto logits = inferenceCallback_(cqtFlat.data(), numFrames, 144);
    if (logits.size() == numFrames * 170) {
      for (std::size_t t = 0; t < numFrames; ++t) {
        const float* frameLogits = logits.data() + t * 170;
        int bestClass = 0;
        float maxVal = frameLogits[0];

        for (int c = 1; c < 170; ++c) {
          if (frameLogits[c] > maxVal) {
            maxVal = frameLogits[c];
            bestClass = c;
          }
        }

        // Softmax approximate confidence
        double sumExp = 0.0;
        for (int c = 0; c < 170; ++c) {
          sumExp += std::exp(std::max(-20.0f, frameLogits[c] - maxVal));
        }
        const float conf = static_cast<float>(1.0 / sumExp);
        frameChords[t] = classIndexToChord(bestClass, conf);
      }
    }
  } else {
    // 4. DSP Fallback path: Fold 144 CQT bins into 12 chroma semitones and match chord templates
    const auto& templates = getTemplates();
    for (std::size_t t = 0; t < numFrames; ++t) {
      std::array<float, 12> chroma{};
      float totalEnergy = 0.0f;

      for (std::size_t b = 0; b < 144; ++b) {
        // 24 bins/octave = 2 bins per semitone
        const int pitchClass = static_cast<int>((b / 2) % 12);
        const float mag = cqtFrames[t][b];
        chroma[pitchClass] += mag;
        totalEnergy += mag;
      }

      if (totalEnergy < 1e-4f) {
        frameChords[t] = {-1, ChordQuality::None, "N", 0.0f};
        continue;
      }

      // Normalize chroma
      float chromaNormSq = 0.0f;
      for (float val : chroma) chromaNormSq += val * val;
      const float invNorm = 1.0f / (std::sqrt(chromaNormSq) + 1e-9f);
      for (float& val : chroma) val *= invNorm;

      int bestRoot = 0;
      int bestQualityIdx = 0;
      float bestScore = -1.0f;

      for (int root = 0; root < 12; ++root) {
        for (const auto& tmpl : templates) {
          float score = 0.0f;
          for (int interval : tmpl.intervals) {
            const int note = (root + interval) % 12;
            score += chroma[note];
          }
          score /= std::sqrt(static_cast<float>(tmpl.intervals.size()));

          if (score > bestScore) {
            bestScore = score;
            bestRoot = root;
            bestQualityIdx = tmpl.qualityIdx;
          }
        }
      }

      if (bestScore > 0.45f) {
        const int classIdx = 1 + bestQualityIdx * 12 + bestRoot;
        frameChords[t] = classIndexToChord(classIdx, std::min(1.0f, bestScore));
      } else {
        frameChords[t] = {-1, ChordQuality::None, "N", 0.0f};
      }
    }
  }

  // 5. Aggregate into ChordSegments
  std::vector<ChordSegment> segments;
  std::size_t segStartFrame = 0;

  for (std::size_t i = 1; i <= numFrames; ++i) {
    const bool isEnd = (i == numFrames);
    const bool changed = !isEnd && (frameChords[i].name != frameChords[segStartFrame].name);

    if (isEnd || changed) {
      ChordSegment seg;
      seg.chord = frameChords[segStartFrame];
      seg.startSec = static_cast<double>(segStartFrame) * hopSec;
      seg.endSec = static_cast<double>(i) * hopSec;
      seg.startFrame = static_cast<std::int64_t>(seg.startSec * static_cast<double>(sampleRate));
      seg.endFrame = static_cast<std::int64_t>(seg.endSec * static_cast<double>(sampleRate));
      segments.push_back(seg);
      segStartFrame = i;
    }
  }

  result.chordProgression = std::move(segments);

  // 6. Compute global chroma profile & dominant chord
  result.chromaProfile.assign(12, 0.0f);
  for (std::size_t t = 0; t < numFrames; ++t) {
    for (std::size_t b = 0; b < 144; ++b) {
      const int pitchClass = static_cast<int>((b / 2) % 12);
      result.chromaProfile[pitchClass] += cqtFrames[t][b];
    }
  }

  std::unordered_map<std::string, double> chordDurations;
  for (const auto& seg : result.chordProgression) {
    if (!seg.chord.isSilent()) {
      const double dur = seg.endSec - seg.startSec;
      chordDurations[seg.chord.name] += dur;
    }
  }

  // Find dominant chord
  std::string domChord = "N";
  double maxDur = 0.0;
  for (const auto& [name, dur] : chordDurations) {
    if (dur > maxDur) {
      maxDur = dur;
      domChord = name;
    }
  }
  result.dominantChord = domChord;

  // Normalize chroma profile
  float chromaSum = std::accumulate(result.chromaProfile.begin(), result.chromaProfile.end(), 0.0f);
  if (chromaSum > 1e-6f) {
    for (float& c : result.chromaProfile) c /= chromaSum;
  }

  // 7. Estimate global MusicalKey from dominant chord or root profile
  if (domChord != "N" && !result.chordProgression.empty()) {
    for (const auto& seg : result.chordProgression) {
      if (seg.chord.name == domChord) {
        const int root = seg.chord.rootNote;
        const bool isMinor = (seg.chord.quality == ChordQuality::Minor ||
                              seg.chord.quality == ChordQuality::Minor7 ||
                              seg.chord.quality == ChordQuality::MinorSixth);
        const int classIdx = isMinor ? (12 + root) : root;
        result.estimatedKey = MusicalKey::fromClassIndex(classIdx, 0.85f);
        break;
      }
    }
  } else {
    result.estimatedKey = MusicalKey::fromCamelot("8A");  // Am fallback
  }

  result.success = !result.chordProgression.empty();
  return result;
}

}  // namespace zyron::analysis
