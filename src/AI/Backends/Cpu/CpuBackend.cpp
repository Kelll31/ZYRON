// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Backends/Cpu/CpuBackend.hpp"

namespace zyron::ai {

std::vector<core::GpuDevice> CpuBackend::getDevices() const {
  std::vector<core::GpuDevice> devices;
  core::GpuDevice cpuDev;
  cpuDev.index = 0;
  cpuDev.name = "Host CPU (Multi-threaded)";
  cpuDev.vramTotalBytes = totalMemoryBytes(0);
  cpuDev.vramFreeBytes = freeMemoryBytes(0);
  cpuDev.computeCapability = "CPU-AVX2/NEON";
  devices.push_back(cpuDev);
  return devices;
}

std::uint64_t CpuBackend::freeMemoryBytes(int /*deviceIndex*/) const {
  // Conservative estimate: 4 GB available memory
  return 4ULL * 1024ULL * 1024ULL * 1024ULL;
}

std::uint64_t CpuBackend::totalMemoryBytes(int /*deviceIndex*/) const {
  // Conservative estimate: 16 GB total host memory
  return 16ULL * 1024ULL * 1024ULL * 1024ULL;
}

}  // namespace zyron::ai
