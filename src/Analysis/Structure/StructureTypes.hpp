// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zyron::analysis {

/// Structural section type of a track (SPEC section 54, ROADMAP P6-01).
enum class SegmentType : std::uint8_t {
  Intro = 0,
  Build,
  Drop,
  Break,
  Drop2,
  Outro
};

[[nodiscard]] constexpr std::string_view segmentTypeName(SegmentType type) noexcept {
  switch (type) {
    case SegmentType::Intro:
      return "Intro";
    case SegmentType::Build:
      return "Build";
    case SegmentType::Drop:
      return "Drop";
    case SegmentType::Break:
      return "Break";
    case SegmentType::Drop2:
      return "Drop 2";
    case SegmentType::Outro:
      return "Outro";
  }
  return "Unknown";
}

/// A contiguous structural segment within a track.
struct TrackSegment {
  SegmentType type{SegmentType::Intro};
  std::int64_t startFrame{0};
  std::int64_t endFrame{0};
  double startSec{0.0};
  double endSec{0.0};
  int startBar{0};           // 1-indexed bar number (if beatgrid is available, otherwise 0)
  int endBar{0};             // 1-indexed bar number (inclusive)
  float averageEnergy{1.0f}; // Average energy rating: 1.0 .. 10.0
  float peakEnergy{1.0f};    // Peak energy rating: 1.0 .. 10.0
};

/// High-level structure model representing all detected phrases/sections of a track.
struct TrackStructure {
  std::vector<TrackSegment> segments;
  double durationSec{0.0};
  std::int64_t totalFrames{0};

  [[nodiscard]] bool empty() const noexcept { return segments.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return segments.size(); }

  [[nodiscard]] std::optional<TrackSegment> findSegmentAtTime(double sec) const noexcept {
    for (const auto& seg : segments) {
      if (sec >= seg.startSec && sec <= seg.endSec) {
        return seg;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<TrackSegment> findSegmentAtFrame(std::int64_t frame) const noexcept {
    for (const auto& seg : segments) {
      if (frame >= seg.startFrame && frame <= seg.endFrame) {
        return seg;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<TrackSegment> intro() const noexcept {
    for (const auto& seg : segments) {
      if (seg.type == SegmentType::Intro) return seg;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<TrackSegment> firstDrop() const noexcept {
    for (const auto& seg : segments) {
      if (seg.type == SegmentType::Drop) return seg;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<TrackSegment> breakdown() const noexcept {
    for (const auto& seg : segments) {
      if (seg.type == SegmentType::Break) return seg;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<TrackSegment> secondDrop() const noexcept {
    for (const auto& seg : segments) {
      if (seg.type == SegmentType::Drop2) return seg;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<TrackSegment> outro() const noexcept {
    for (const auto& seg : segments) {
      if (seg.type == SegmentType::Outro) return seg;
    }
    return std::nullopt;
  }
};

/// Mix-point cue marker for DJ transitions (SPEC sections 25, 54, ROADMAP P6-03).
struct MixPointCue {
  std::int64_t frame{0};
  double timeSec{0.0};
  std::string name;          // e.g. "Intro", "Drop 1", "Breakdown", "Drop 2", "Outro"
  std::string color;         // Hex color string (e.g. "#00FF88")
  std::string cueType;       // "intro", "drop", "break", "outro"
  int hotCueSlot{0};         // Slot index 0..7
};

}  // namespace zyron::analysis
