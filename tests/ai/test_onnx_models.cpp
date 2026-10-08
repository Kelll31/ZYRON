// SPDX-License-Identifier: AGPL-3.0-only
//
// Smoke test of the downloaded neural networks (docs/AI_MODELS.md) through the ONNX Runtime backend: each model loads
// on the CPU and on DirectML, takes an input of the documented shape and returns the documented output. Skipped when
// the weights are not on disk (run scripts/download_models.ps1 first); never needs a GPU to pass.
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <string>

#include "AI/Backends/Cpu/CpuBackend.hpp"
#include "AI/Backends/Onnx/OnnxRuntime.hpp"

using namespace zyron::ai;
namespace fs = std::filesystem;

namespace {

fs::path modelsDir() {
  return fs::path(ZYRON_MODELS_DIR);
}

double seconds(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

Tensor filled(std::vector<std::int64_t> dims, float scale) {
  Tensor tensor{TensorShape(std::move(dims))};
  for (std::size_t i = 0; i < tensor.size(); ++i) {
    tensor[i] = scale * static_cast<float>(std::sin(0.01 * static_cast<double>(i)));
  }
  return tensor;
}

struct Case {
  const char* name;
  const char* file;
  std::vector<std::int64_t> inputShape;
  std::size_t expectedOutputs;
  std::vector<std::int64_t> firstOutputShape;
};

}  // namespace

TEST_CASE("ONNX models load and run on CPU and DirectML (skipped without weights)", "[ai][onnx][models]") {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // progress lines must appear even if a slow model is killed
  OnnxRuntime runtime;
  CpuBackend cpu;
  DirectMlBackend directMl;
  INFO(runtime.name());

  const std::vector<Case> cases = {
      {"S-KEY", "skey/skey.onnx", {1, 22050 * 10}, 1, {24}},
      {"ChordMini", "chordmini/chordnet.onnx", {16, 108, 144}, 1, {16, 108, 170}},
      {"Beat This!", "beat-this/beat_this.onnx", {1, 1500, 128}, 2, {1, 1500}},
      {"HTDemucs", "htdemucs/htdemucs.onnx", {1, 2, 343980}, 1, {1, 4, 2, 343980}},
  };

  for (const Case& c : cases) {
    const fs::path path = modelsDir() / c.file;
    if (!fs::exists(path)) {
      WARN("model not on disk, skipped: " << path.string());
      continue;
    }
    SECTION(c.name) {
      const Tensor input = filled(c.inputShape, 0.1F);

      // ZYRON_ONNX_BACKEND=cpu|dml limits the run (the big models are slow on the CPU).
      const char* only = std::getenv("ZYRON_ONNX_BACKEND");
      for (const GPUBackend* backend : {static_cast<const GPUBackend*>(&cpu), static_cast<const GPUBackend*>(&directMl)}) {
        if (only != nullptr && std::string(only) != (backend->type() == BackendType::Cpu ? "cpu" : "dml")) {
          continue;
        }
        // HTDemucs on DirectML gave no result in 150 s on the first run (ADR-0015): not part of the default run.
        if (only == nullptr && backend->type() == BackendType::DirectML && std::string(c.name) == "HTDemucs") {
          std::printf("  HTDemucs on DirectML: skipped (set ZYRON_ONNX_BACKEND=dml to try)\n");
          continue;
        }
        std::printf("  %s on %s: loading...\n", c.name, std::string(backendTypeName(backend->type())).c_str());
        const auto loadStart = std::chrono::steady_clock::now();
        auto session = runtime.createSession(path.string(), *backend, 0);
        if (session == nullptr) {
          // A model that one device cannot run is a finding, not a failure: the CPU path must always work.
          std::printf("  %-11s %-8s load FAILED: %s\n", c.name, std::string(backendTypeName(backend->type())).c_str(),
                      runtime.lastError().c_str());
          CHECK(backend->type() != BackendType::Cpu);
          continue;
        }
        const double loadSeconds = seconds(loadStart);

        std::printf("    loaded in %.1fs, running...\n", loadSeconds);
        const auto runStart = std::chrono::steady_clock::now();
        const auto outputs = session->run({input});
        const double firstRun = seconds(runStart);
        const auto secondStart = std::chrono::steady_clock::now();
        (void)session->run({input});
        const double secondRun = seconds(secondStart);

        std::printf("  %-11s %-8s load %.2fs  run1 %.2fs  run2 %.2fs\n", c.name,
                    std::string(backendTypeName(backend->type())).c_str(), loadSeconds, firstRun, secondRun);

        REQUIRE(outputs.size() == c.expectedOutputs);
        CHECK(outputs[0].shape().dims == c.firstOutputShape);
        for (std::size_t i = 0; i < outputs[0].size(); i += outputs[0].size() / 64 + 1) {
          CHECK(std::isfinite(outputs[0][i]));
        }
      }
    }
  }
}
