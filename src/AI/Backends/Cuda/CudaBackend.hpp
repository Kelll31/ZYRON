// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "AI/Backends/GPUBackend.hpp"
#include "Core/System/DynamicLibrary.hpp"
#include "Core/System/HardwareInfo.hpp"
#include "Core/System/HardwareProbe.hpp"

namespace zyron::ai {

/// CUDA GPU compute backend supporting dynamic discovery and multi-GPU enumeration (SPEC sections 34, 35, 36).
///
/// Discovers NVIDIA GPUs at runtime using dynamic NVML loading (no link-time CUDA dependency, SPEC section 37).
/// Supports compute capability queries, VRAM introspection, and fp16 capability detection.
class CudaBackend final : public GPUBackend {
 public:
  CudaBackend();
  explicit CudaBackend(std::unique_ptr<core::GpuProbe> probe);
  ~CudaBackend() override = default;

  [[nodiscard]] BackendType type() const noexcept override { return BackendType::Cuda; }
  [[nodiscard]] bool isAvailable() const noexcept override;
  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string version() const override;
  [[nodiscard]] bool supportsFp16() const noexcept override;

  [[nodiscard]] std::vector<core::GpuDevice> getDevices() const override;
  [[nodiscard]] std::uint64_t freeMemoryBytes(int deviceIndex = 0) const override;
  [[nodiscard]] std::uint64_t totalMemoryBytes(int deviceIndex = 0) const override;

  [[nodiscard]] const core::GpuInfo& gpuInfo() const noexcept { return info_; }
  void refresh() noexcept;

 private:
  std::unique_ptr<core::GpuProbe> probe_;
  core::GpuInfo info_{};
};

}  // namespace zyron::ai
