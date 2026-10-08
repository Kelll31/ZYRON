// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <vector>

#include "Core/System/DynamicLibrary.hpp"
#include "Core/System/HardwareProbe.hpp"

namespace zyron::ai {

/// NVML's driver-version encoding 1000*major + 10*minor -> "13.4"; zero or negative -> "" (unknown).
[[nodiscard]] std::string formatCudaVersion(int nvmlVersion);

/// Lists NVIDIA GPUs through NVML (the library behind `nvidia-smi`), loaded at run time. No CUDA toolkit, no link
/// dependency: without an NVIDIA driver the probe simply reports Status::NoDriver and ZYRON carries on with the CPU
/// backend (SPEC sections 35, 37, 40).
///
/// The few NVML types needed are declared locally (their ABI is stable), so the NVIDIA headers are not required.
/// `GpuDevice::index` is NVML's index (PCI bus order), not a CUDA ordinal.
class NvmlGpuProbe final : public core::GpuProbe {
 public:
  /// `libraryNames` are tried in order; the first that `loader` can open is used. Names must come from code, never
  /// from the UI, the AI or the network (SPEC section 76).
  NvmlGpuProbe(core::LibraryLoader loader, std::vector<std::string> libraryNames);

  [[nodiscard]] core::GpuInfo probe() override;

 private:
  core::LibraryLoader loader_;
  std::vector<std::string> libraryNames_;
};

}  // namespace zyron::ai
