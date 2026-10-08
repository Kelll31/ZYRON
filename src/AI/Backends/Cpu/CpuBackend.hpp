// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AI/Backends/GPUBackend.hpp"
#include "Core/System/HardwareInfo.hpp"

namespace zyron::ai {

/// CPU reference compute backend for universal fallback on machines without dedicated GPU (SPEC section 40).
class CpuBackend final : public GPUBackend {
 public:
  CpuBackend() = default;
  ~CpuBackend() override = default;

  [[nodiscard]] BackendType type() const noexcept override { return BackendType::Cpu; }
  [[nodiscard]] bool isAvailable() const noexcept override { return true; }
  [[nodiscard]] std::string name() const override { return "Host CPU"; }
  [[nodiscard]] std::string version() const override { return "Native C++20"; }
  [[nodiscard]] bool supportsFp16() const noexcept override { return false; }

  [[nodiscard]] std::vector<core::GpuDevice> getDevices() const override;
  [[nodiscard]] std::uint64_t freeMemoryBytes(int deviceIndex = 0) const override;
  [[nodiscard]] std::uint64_t totalMemoryBytes(int deviceIndex = 0) const override;
};

}  // namespace zyron::ai
