// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "AI/Backends/Cpu/CpuBackend.hpp"
#include "AI/Backends/GPUBackend.hpp"
#include "AI/Runtime/AIRuntime.hpp"
#include "AI/Runtime/Tensor.hpp"
#include "Stems/MockStemSeparator.hpp"
#include "Stems/StemSeparator.hpp"
#include "Stems/StemTypes.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron;

TEST_CASE("GPUBackend and CpuBackend fallback discovery and capabilities", "[ai][backend]") {
  ai::CpuBackend cpu;

  SECTION("Basic identification and status") {
    CHECK(cpu.type() == ai::BackendType::Cpu);
    CHECK(cpu.isAvailable());
    CHECK(cpu.name() == "Host CPU");
    CHECK_FALSE(cpu.version().empty());
    CHECK_FALSE(cpu.supportsFp16());
  }

  SECTION("Reports host CPU device enumeration") {
    const auto devices = cpu.getDevices();
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].index == 0);
    CHECK_FALSE(devices[0].name.empty());
    CHECK(devices[0].vramTotalBytes > 0);
    CHECK(devices[0].vramFreeBytes > 0);
  }

  SECTION("Memory query methods return valid values") {
    CHECK(cpu.totalMemoryBytes(0) >= cpu.freeMemoryBytes(0));
    CHECK(cpu.freeMemoryBytes(0) > 0);
  }

  SECTION("Backend type names are human-readable") {
    CHECK(ai::backendTypeName(ai::BackendType::Cpu) == "CPU");
    CHECK(ai::backendTypeName(ai::BackendType::Cuda) == "CUDA");
    CHECK(ai::backendTypeName(ai::BackendType::Metal) == "Metal");
    CHECK(ai::backendTypeName(ai::BackendType::Rocm) == "ROCm");
    CHECK(ai::backendTypeName(ai::BackendType::DirectML) == "DirectML");
  }
}

TEST_CASE("TensorShape and Tensor buffer operations", "[ai][tensor]") {
  SECTION("TensorShape dimensionality and element calculation") {
    const ai::TensorShape shape({1, 4, 256});
    CHECK(shape.rank() == 3);
    CHECK(shape.dim(0) == 1);
    CHECK(shape.dim(1) == 4);
    CHECK(shape.dim(2) == 256);
    CHECK(shape.totalElements() == 1024);
    CHECK_THROWS_AS(shape.dim(3), std::out_of_range);
  }

  SECTION("Tensor default construction and zero-initialization") {
    const ai::TensorShape shape({2, 10});
    ai::Tensor tensor(shape);

    CHECK(tensor.size() == 20);
    CHECK_FALSE(tensor.empty());
    CHECK(tensor.type() == ai::DataType::Float32);

    for (std::size_t i = 0; i < tensor.size(); ++i) {
      CHECK(tensor[i] == 0.0f);
    }
  }

  SECTION("Tensor population, data access, and reshape") {
    const ai::TensorShape shape({2, 3});
    std::vector<float> values{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    ai::Tensor tensor(shape, values);

    CHECK(tensor.at(0) == 1.0f);
    CHECK(tensor.at(5) == 6.0f);

    tensor[2] = 42.0f;
    CHECK(tensor.at(2) == 42.0f);

    // Reshape to 1x6
    tensor.reshape(ai::TensorShape({1, 6}));
    CHECK(tensor.shape().rank() == 2);
    CHECK(tensor.shape().dim(0) == 1);
    CHECK(tensor.shape().dim(1) == 6);

    // Invalid reshape element mismatch throws
    CHECK_THROWS_AS(tensor.reshape(ai::TensorShape({2, 4})), std::invalid_argument);
  }

  SECTION("Tensor mismatch on construction throws") {
    const ai::TensorShape shape({2, 2});
    std::vector<float> values{1.0f, 2.0f, 3.0f};  // 3 instead of 4
    CHECK_THROWS_AS(ai::Tensor(shape, values), std::invalid_argument);
  }
}

TEST_CASE("StemSeparator interface and MockStemSeparator execution", "[stems][separator]") {
  stems::MockStemSeparator separator;

  SECTION("Separator metadata contract") {
    CHECK_FALSE(separator.modelName().empty());
    CHECK_FALSE(separator.modelVersion().empty());
    CHECK(separator.requiredSampleRate() == 44100.0);
  }

  SECTION("Rejects invalid inputs gracefully") {
    auto res1 = separator.separate(nullptr, 2, 44100, 44100.0);
    CHECK_FALSE(res1.success);
    CHECK_FALSE(res1.error.empty());

    float dummy[100]{};
    const float* chs[1]{dummy};
    auto res2 = separator.separate(chs, 0, 44100, 44100.0);
    CHECK_FALSE(res2.success);

    auto res3 = separator.separate(chs, 1, 0, 44100.0);
    CHECK_FALSE(res3.success);
  }

  SECTION("Separates synthetic multi-frequency audio into 4 stems") {
    constexpr double sampleRate = 44100.0;
    constexpr int numFrames = 44100;  // 1.0 second
    std::vector<float> leftInput(numFrames);
    std::vector<float> rightInput(numFrames);

    // Synthesize test signal:
    //  - 80 Hz pure bass tone
    //  - 1000 Hz mid tone (Vocals range)
    //  - 8000 Hz high frequency (Drums hi-hat range)
    for (int i = 0; i < numFrames; ++i) {
      const double t = static_cast<double>(i) / sampleRate;
      const float bassSine = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 80.0 * t));
      const float midSine = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 1000.0 * t));
      const float highSine = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 8000.0 * t));

      leftInput[static_cast<std::size_t>(i)] = 0.5f * bassSine + 0.3f * midSine + 0.2f * highSine;
      rightInput[static_cast<std::size_t>(i)] = leftInput[static_cast<std::size_t>(i)];
    }

    const float* channels[2] = {leftInput.data(), rightInput.data()};

    std::vector<float> progressReports;
    auto result = separator.separate(channels, 2, numFrames, sampleRate,
                                     [&](float progress) { progressReports.push_back(progress); });

    REQUIRE(result.success);
    CHECK(result.error.empty());
    CHECK(result.sampleRate == sampleRate);
    CHECK(result.numFrames == numFrames);
    CHECK(result.processingDurationSec >= 0.0);

    // Verify all 4 stems exist and have correct length
    CHECK(result.vocals().numFrames() == numFrames);
    CHECK(result.drums().numFrames() == numFrames);
    CHECK(result.bass().numFrames() == numFrames);
    CHECK(result.other().numFrames() == numFrames);

    // Verify energy presence in bass and vocals
    double bassRms = 0.0;
    for (float sample : result.bass().left) {
      bassRms += sample * sample;
    }
    bassRms = std::sqrt(bassRms / numFrames);
    CHECK(bassRms > 0.1);  // Bass stem isolated substantial energy

    // Verify progress reported
    REQUIRE_FALSE(progressReports.empty());
    CHECK_THAT(progressReports.back(), WithinAbs(1.0f, 1e-4f));
  }

  SECTION("StemSlot names and indexing helper contracts") {
    CHECK(stems::stemSlotName(stems::StemSlot::Vocals) == "Vocals");
    CHECK(stems::stemSlotName(stems::StemSlot::Drums) == "Drums");
    CHECK(stems::stemSlotName(stems::StemSlot::Bass) == "Bass");
    CHECK(stems::stemSlotName(stems::StemSlot::Other) == "Other");
    CHECK(stems::kStemCount == 4);
  }
}
