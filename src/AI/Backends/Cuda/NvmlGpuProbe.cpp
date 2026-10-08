// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Backends/Cuda/NvmlGpuProbe.hpp"

#include <array>
#include <string_view>
#include <type_traits>
#include <utility>

namespace zyron::ai {
namespace {

// The slice of the NVML C API we use. Signatures and value types are part of NVIDIA's stable ABI; declaring them here
// avoids a dependency on nvml.h (which ships only with the CUDA toolkit).
using NvmlReturn = int;
using NvmlDevice = void*;
struct NvmlMemory {
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
};
static_assert(sizeof(NvmlMemory) == 24 && std::is_standard_layout_v<NvmlMemory>,
              "NvmlMemory must match nvmlMemory_t (three 64-bit counters) exactly");

using InitFn = NvmlReturn (*)();
using ShutdownFn = NvmlReturn (*)();
using DeviceCountFn = NvmlReturn (*)(unsigned*);
using DeviceHandleFn = NvmlReturn (*)(unsigned, NvmlDevice*);
using DeviceNameFn = NvmlReturn (*)(NvmlDevice, char*, unsigned);
using MemoryInfoFn = NvmlReturn (*)(NvmlDevice, NvmlMemory*);
using ComputeCapabilityFn = NvmlReturn (*)(NvmlDevice, int*, int*);
using DriverVersionFn = NvmlReturn (*)(char*, unsigned);
using CudaDriverVersionFn = NvmlReturn (*)(int*);

constexpr NvmlReturn kNvmlSuccess = 0;
constexpr NvmlReturn kNvmlDriverNotLoaded = 9;
constexpr std::size_t kNameBufferSize = 96;           // NVML_DEVICE_NAME_V2_BUFFER_SIZE
constexpr std::size_t kDriverVersionBufferSize = 80;  // NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE
constexpr const char* kVendor = "NVIDIA";

struct NvmlApi {
  // Required.
  InitFn init{nullptr};
  ShutdownFn shutdown{nullptr};
  DeviceCountFn deviceCount{nullptr};
  DeviceHandleFn deviceHandle{nullptr};
  DeviceNameFn deviceName{nullptr};
  MemoryInfoFn memoryInfo{nullptr};
  // Optional: absent on old drivers; the report then shows "unknown" for what they would have provided.
  ComputeCapabilityFn computeCapability{nullptr};
  DriverVersionFn driverVersion{nullptr};
  CudaDriverVersionFn cudaDriverVersion{nullptr};
};

template <class Fn>
Fn symbolAs(const core::DynamicLibrary& library, std::string_view name) {
  return reinterpret_cast<Fn>(library.findSymbol(name));
}

core::GpuInfo failure(core::GpuInfo::Status status, std::string detail) {
  core::GpuInfo info;
  info.status = status;
  info.vendor = kVendor;
  info.detail = std::move(detail);
  return info;
}

void appendNote(std::string& notes, const std::string& note) {
  notes += (notes.empty() ? "" : "; ") + note;
}

/// Calls nvmlShutdown when it goes out of scope; constructed only after nvmlInit succeeded.
class NvmlSession {
 public:
  explicit NvmlSession(ShutdownFn shutdown) : shutdown_(shutdown) {}
  ~NvmlSession() { shutdown_(); }
  NvmlSession(const NvmlSession&) = delete;
  NvmlSession& operator=(const NvmlSession&) = delete;

 private:
  ShutdownFn shutdown_;
};

std::unique_ptr<core::DynamicLibrary> openFirst(const core::LibraryLoader& loader,
                                                const std::vector<std::string>& names, std::string& tried) {
  for (const std::string& name : names) {
    if (auto library = loader ? loader(name) : nullptr) {
      return library;
    }
    appendNote(tried, name);
  }
  return nullptr;
}

/// Fills `api`; returns the name of the first required function that is missing, or "" when all are present.
std::string resolveApi(const core::DynamicLibrary& library, NvmlApi& api) {
  api.init = symbolAs<InitFn>(library, "nvmlInit_v2");
  api.shutdown = symbolAs<ShutdownFn>(library, "nvmlShutdown");
  api.deviceCount = symbolAs<DeviceCountFn>(library, "nvmlDeviceGetCount_v2");
  api.deviceHandle = symbolAs<DeviceHandleFn>(library, "nvmlDeviceGetHandleByIndex_v2");
  api.deviceName = symbolAs<DeviceNameFn>(library, "nvmlDeviceGetName");
  api.memoryInfo = symbolAs<MemoryInfoFn>(library, "nvmlDeviceGetMemoryInfo");
  api.computeCapability = symbolAs<ComputeCapabilityFn>(library, "nvmlDeviceGetCudaComputeCapability");
  api.driverVersion = symbolAs<DriverVersionFn>(library, "nvmlSystemGetDriverVersion");
  api.cudaDriverVersion = symbolAs<CudaDriverVersionFn>(library, "nvmlSystemGetCudaDriverVersion");

  const std::array<std::pair<bool, const char*>, 6> required = {{
      {api.init != nullptr, "nvmlInit_v2"},
      {api.shutdown != nullptr, "nvmlShutdown"},
      {api.deviceCount != nullptr, "nvmlDeviceGetCount_v2"},
      {api.deviceHandle != nullptr, "nvmlDeviceGetHandleByIndex_v2"},
      {api.deviceName != nullptr, "nvmlDeviceGetName"},
      {api.memoryInfo != nullptr, "nvmlDeviceGetMemoryInfo"},
  }};
  for (const auto& [present, name] : required) {
    if (!present) {
      return name;
    }
  }
  return {};
}

void readSystemInfo(const NvmlApi& api, core::GpuInfo& info) {
  if (api.driverVersion != nullptr) {
    std::array<char, kDriverVersionBufferSize> buffer{};
    if (api.driverVersion(buffer.data(), static_cast<unsigned>(buffer.size())) == kNvmlSuccess) {
      buffer.back() = '\0';  // do not rely on NVML terminating the string
      info.driverVersion = buffer.data();
    }
  }
  if (api.cudaDriverVersion != nullptr) {
    int version = 0;
    if (api.cudaDriverVersion(&version) == kNvmlSuccess && version > 0) {
      info.computeApi = "CUDA " + formatCudaVersion(version);
    }
  }
}

core::GpuDevice readDevice(const NvmlApi& api, NvmlDevice handle, unsigned index, std::string& notes) {
  core::GpuDevice device;
  device.index = static_cast<int>(index);
  const std::string label = "device " + std::to_string(index);

  std::array<char, kNameBufferSize> name{};
  if (api.deviceName(handle, name.data(), static_cast<unsigned>(name.size())) == kNvmlSuccess) {
    name.back() = '\0';
    device.name = name.data();
  } else {
    device.name = "unknown";
    appendNote(notes, label + ": name not readable");
  }

  NvmlMemory memory{};
  if (api.memoryInfo(handle, &memory) == kNvmlSuccess) {
    device.vramTotalBytes = memory.total;
    device.vramFreeBytes = memory.free;
  } else {
    appendNote(notes, label + ": memory not readable");
  }

  if (api.computeCapability != nullptr) {
    int major = 0;
    int minor = 0;
    if (api.computeCapability(handle, &major, &minor) == kNvmlSuccess) {
      device.computeCapability = std::to_string(major) + "." + std::to_string(minor);
    }
  }
  return device;
}

}  // namespace

std::string formatCudaVersion(int nvmlVersion) {
  if (nvmlVersion <= 0) {
    return {};
  }
  return std::to_string(nvmlVersion / 1000) + "." + std::to_string((nvmlVersion % 1000) / 10);
}

NvmlGpuProbe::NvmlGpuProbe(core::LibraryLoader loader, std::vector<std::string> libraryNames)
    : loader_(std::move(loader)), libraryNames_(std::move(libraryNames)) {}

core::GpuInfo NvmlGpuProbe::probe() {
  std::string tried;
  const std::unique_ptr<core::DynamicLibrary> library = openFirst(loader_, libraryNames_, tried);
  if (!library) {
    return failure(core::GpuInfo::Status::NoDriver,
                   "NVIDIA management library not found" + (tried.empty() ? std::string{} : " (tried: " + tried + ")"));
  }

  NvmlApi api;
  if (const std::string missing = resolveApi(*library, api); !missing.empty()) {
    return failure(core::GpuInfo::Status::Error, "NVML is missing the required function " + missing);
  }

  if (const NvmlReturn result = api.init(); result != kNvmlSuccess) {
    if (result == kNvmlDriverNotLoaded) {
      return failure(core::GpuInfo::Status::NoDriver, "the NVIDIA driver is not loaded");
    }
    return failure(core::GpuInfo::Status::Error, "nvmlInit failed with code " + std::to_string(result));
  }
  const NvmlSession session(api.shutdown);  // from here on NVML is always shut down, before `library` is released

  core::GpuInfo info = failure(core::GpuInfo::Status::Available, {});
  readSystemInfo(api, info);

  unsigned count = 0;
  if (const NvmlReturn result = api.deviceCount(&count); result != kNvmlSuccess) {
    info.status = core::GpuInfo::Status::Error;
    info.detail = "nvmlDeviceGetCount failed with code " + std::to_string(result);
    return info;  // keeps the driver details already read
  }
  if (count == 0) {
    info.status = core::GpuInfo::Status::NoDevices;
    info.detail = "NVML reports no devices";
    return info;
  }

  std::string notes;
  for (unsigned i = 0; i < count; ++i) {
    NvmlDevice handle = nullptr;
    if (api.deviceHandle(i, &handle) != kNvmlSuccess) {
      appendNote(notes, "device " + std::to_string(i) + " not accessible");
      continue;
    }
    info.devices.push_back(readDevice(api, handle, i, notes));
  }

  info.detail = std::move(notes);
  info.status = info.devices.empty() ? core::GpuInfo::Status::Error : core::GpuInfo::Status::Available;
  return info;
}

}  // namespace zyron::ai
