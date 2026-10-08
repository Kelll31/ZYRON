// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "AI/Backends/GPUBackend.hpp"
#include "AI/Runtime/AIRuntime.hpp"

namespace zyron::ai {

/// DirectML compute backend: any DirectX 12 GPU on Windows (NVIDIA, AMD, Intel) through ONNX Runtime's DirectML
/// execution provider, without a CUDA or cuDNN installation (ADR-0014). The device index is the DXGI adapter index.
class DirectMlBackend final : public GPUBackend {
 public:
  [[nodiscard]] BackendType type() const noexcept override { return BackendType::DirectML; }
  [[nodiscard]] bool isAvailable() const noexcept override;
  [[nodiscard]] std::string name() const override { return "DirectML"; }
  [[nodiscard]] std::string version() const override;
  [[nodiscard]] bool supportsFp16() const noexcept override { return true; }
  [[nodiscard]] std::vector<core::GpuDevice> getDevices() const override { return {}; }  // see NvmlGpuProbe
  [[nodiscard]] std::uint64_t freeMemoryBytes(int) const override { return 0; }
  [[nodiscard]] std::uint64_t totalMemoryBytes(int) const override { return 0; }
};

/// ONNX Runtime implementation of the AIRuntime interface (SPEC sections 33, 34, ADR-0006, ADR-0014).
///
/// Only float32 tensors are supported (every model in docs/AI_MODELS.md takes and returns float32). Creating a
/// session can fail (missing file, unsupported operator on the chosen device): the factory then returns nullptr and
/// lastError() says why, so callers can fall back to the CPU backend.
class OnnxRuntime final : public AIRuntime {
 public:
  OnnxRuntime();
  ~OnnxRuntime() override;

  [[nodiscard]] RuntimeType type() const noexcept override { return RuntimeType::OnnxRuntime; }
  [[nodiscard]] std::string name() const override;
  [[nodiscard]] bool isAvailable() const noexcept override { return true; }

  std::unique_ptr<IModelSession> createSession(const std::string& modelPath, const GPUBackend& backend,
                                               int deviceIndex = 0) override;
  std::unique_ptr<IModelSession> createSessionFromMemory(const void* data, std::size_t sizeBytes,
                                                         const GPUBackend& backend, int deviceIndex = 0) override;

  /// Why the last createSession* call returned nullptr. Empty after a success.
  [[nodiscard]] std::string lastError() const { return lastError_; }

 private:
  std::string lastError_;
};

}  // namespace zyron::ai
