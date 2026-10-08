// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Backends/Cuda/CudaBackend.hpp"

#include <charconv>
#include <string_view>
#include <utility>

#include "AI/Backends/Cuda/NvmlGpuProbe.hpp"

namespace zyron::ai {

namespace {

std::vector<std::string> defaultNvmlLibraries() {
  return {"nvml.dll", "libnvidia-ml.so.1"};
}

}  // namespace

CudaBackend::CudaBackend()
    : probe_(std::make_unique<NvmlGpuProbe>(core::openDynamicLibrary, defaultNvmlLibraries())) {
  refresh();
}

CudaBackend::CudaBackend(std::unique_ptr<core::GpuProbe> probe) : probe_(std::move(probe)) {
  refresh();
}

void CudaBackend::refresh() noexcept {
  if (probe_ != nullptr) {
    info_ = probe_->probe();
  }
}

bool CudaBackend::isAvailable() const noexcept {
  return info_.status == core::GpuInfo::Status::Available && !info_.devices.empty();
}

std::string CudaBackend::name() const {
  return "NVIDIA CUDA Backend";
}

std::string CudaBackend::version() const {
  if (!info_.computeApi.empty()) {
    return info_.computeApi;
  }
  if (!info_.driverVersion.empty()) {
    return "Driver " + info_.driverVersion;
  }
  return "CUDA";
}

bool CudaBackend::supportsFp16() const noexcept {
  if (!isAvailable()) {
    return false;
  }
  for (const auto& dev : info_.devices) {
    if (dev.computeCapability.empty()) {
      continue;
    }
    // Parse compute capability (e.g. "8.6" -> major 8)
    int major = 0;
    const auto [ptr, ec] = std::from_chars(dev.computeCapability.data(),
                                           dev.computeCapability.data() + dev.computeCapability.size(), major);
    if (ec == std::errc{} && major >= 7) {
      return true;  // Volta (7.0), Turing (7.5), Ampere (8.0, 8.6), Ada (8.9), Hopper (9.0)
    }
  }
  return false;
}

std::vector<core::GpuDevice> CudaBackend::getDevices() const {
  return info_.devices;
}

std::uint64_t CudaBackend::freeMemoryBytes(int deviceIndex) const {
  if (deviceIndex >= 0 && static_cast<std::size_t>(deviceIndex) < info_.devices.size()) {
    return info_.devices[static_cast<std::size_t>(deviceIndex)].vramFreeBytes;
  }
  return 0;
}

std::uint64_t CudaBackend::totalMemoryBytes(int deviceIndex) const {
  if (deviceIndex >= 0 && static_cast<std::size_t>(deviceIndex) < info_.devices.size()) {
    return info_.devices[static_cast<std::size_t>(deviceIndex)].vramTotalBytes;
  }
  return 0;
}

}  // namespace zyron::ai
