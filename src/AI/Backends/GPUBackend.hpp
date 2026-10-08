// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Core/System/HardwareInfo.hpp"

namespace zyron::ai {

/// Supported GPU and accelerator compute backends (SPEC sections 34, 37, 79).
enum class BackendType : std::uint8_t {
  Cpu = 0,
  Cuda,
  Metal,
  Rocm,
  DirectML
};

[[nodiscard]] constexpr std::string_view backendTypeName(BackendType type) noexcept {
  switch (type) {
    case BackendType::Cpu:
      return "CPU";
    case BackendType::Cuda:
      return "CUDA";
    case BackendType::Metal:
      return "Metal";
    case BackendType::Rocm:
      return "ROCm";
    case BackendType::DirectML:
      return "DirectML";
  }
  return "Unknown";
}

/// Abstract GPU and compute acceleration backend interface (SPEC sections 34, 79).
/// Allows discovery, capability query, and execution without binding Core or callers to vendor SDKs.
class GPUBackend {
 public:
  virtual ~GPUBackend() = default;

  [[nodiscard]] virtual BackendType type() const noexcept = 0;
  [[nodiscard]] virtual bool isAvailable() const noexcept = 0;
  [[nodiscard]] virtual std::string name() const = 0;
  [[nodiscard]] virtual std::string version() const = 0;
  [[nodiscard]] virtual bool supportsFp16() const noexcept = 0;

  /// Returns enumeration of available physical devices managed by this backend.
  [[nodiscard]] virtual std::vector<core::GpuDevice> getDevices() const = 0;

  /// Allocates or queries free VRAM in bytes on deviceIndex. Returns 0 if unsupported/unknown.
  [[nodiscard]] virtual std::uint64_t freeMemoryBytes(int deviceIndex = 0) const = 0;
  [[nodiscard]] virtual std::uint64_t totalMemoryBytes(int deviceIndex = 0) const = 0;
};

}  // namespace zyron::ai
