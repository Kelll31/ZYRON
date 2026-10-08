// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <string>

#include "Stems/StemSeparator.hpp"

namespace zyron::stems {

/// Reference DSP-based 4-stem separator for CPU fallback, regression testing, and mock execution (SPEC section 33, 40).
class MockStemSeparator final : public StemSeparator {
 public:
  MockStemSeparator() = default;
  ~MockStemSeparator() override = default;

  [[nodiscard]] std::string modelName() const override { return "Reference-DSP-Separator"; }
  [[nodiscard]] std::string modelVersion() const override { return "1.0.0"; }
  [[nodiscard]] double requiredSampleRate() const override { return 44100.0; }

  StemSeparationResult separate(const float* const* inputChannels,
                                int numChannels,
                                std::int64_t numFrames,
                                double sampleRate,
                                std::function<void(float progress)> progressCallback = nullptr) override;
};

}  // namespace zyron::stems
