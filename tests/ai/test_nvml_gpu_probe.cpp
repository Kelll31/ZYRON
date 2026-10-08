// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AI/Backends/Cuda/NvmlGpuProbe.hpp"
#include "Platform/GpuLibraries.hpp"

using namespace zyron;
using namespace zyron::core;

namespace {

// ---- A fake NVML. Signatures mirror the real C API (see NvmlGpuProbe.cpp); behaviour is driven by `g`. ----

using NvmlReturn = int;
using NvmlDevice = void*;
struct NvmlMemory {
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
};

constexpr NvmlReturn kSuccess = 0;
constexpr NvmlReturn kDriverNotLoaded = 9;
constexpr NvmlReturn kUnknown = 999;

struct FakeNvml {
  NvmlReturn initResult{kSuccess};
  unsigned deviceCount{2};
  NvmlReturn nameResult{kSuccess};
  NvmlReturn countResult{kSuccess};
  NvmlReturn handleResult{kSuccess};
  NvmlReturn memoryResult{kSuccess};
  bool noTerminator{false};      // misbehaving library: fills the whole buffer without a trailing NUL
  unsigned failHandleFrom{~0U};  // handle calls with index >= this fail (partial failure)
  int initCalls{0};
  int shutdownCalls{0};
  std::string driver{"576.52"};
  int cudaVersion{13040};
  std::vector<std::string> names{"NVIDIA GeForce RTX 3090", "NVIDIA GeForce RTX 3090 Ti"};
};
FakeNvml g;

NvmlReturn fakeInit() {
  ++g.initCalls;
  return g.initResult;
}
NvmlReturn fakeShutdown() {
  ++g.shutdownCalls;
  return kSuccess;
}
NvmlReturn fakeCount(unsigned* count) {
  *count = g.deviceCount;
  return g.countResult;
}
NvmlReturn fakeHandle(unsigned index, NvmlDevice* device) {
  if (g.handleResult != kSuccess || index >= g.failHandleFrom) {
    return g.handleResult != kSuccess ? g.handleResult : kUnknown;
  }
  *device = reinterpret_cast<NvmlDevice>(static_cast<std::uintptr_t>(index) + 1);
  return kSuccess;
}
/// Bounded, always NUL-terminated copy (the real NVML does the same).
void copyTo(char* out, unsigned length, const std::string& text) {
  if (g.noTerminator) {
    std::memset(out, 'x', length);  // a probe that trusts the library would read past the buffer
    return;
  }
  const std::size_t count = std::min<std::size_t>(text.size(), length - 1);
  std::memcpy(out, text.data(), count);
  out[count] = '\0';
}

NvmlReturn fakeName(NvmlDevice device, char* out, unsigned length) {
  if (g.nameResult != kSuccess) {
    return g.nameResult;
  }
  const auto index = reinterpret_cast<std::uintptr_t>(device) - 1;
  copyTo(out, length, g.names.at(index));
  return kSuccess;
}
NvmlReturn fakeMemory(NvmlDevice, NvmlMemory* memory) {
  if (g.memoryResult != kSuccess) {
    return g.memoryResult;
  }
  memory->total = 24ULL * 1024 * 1024 * 1024;
  memory->free = 20ULL * 1024 * 1024 * 1024;
  memory->used = memory->total - memory->free;
  return kSuccess;
}
NvmlReturn fakeCompute(NvmlDevice, int* major, int* minor) {
  *major = 8;
  *minor = 6;
  return kSuccess;
}
NvmlReturn fakeDriverVersion(char* out, unsigned length) {
  copyTo(out, length, g.driver);
  return kSuccess;
}
NvmlReturn fakeCudaVersion(int* version) {
  *version = g.cudaVersion;
  return kSuccess;
}

class FakeLibrary final : public DynamicLibrary {
 public:
  void* findSymbol(std::string_view name) const noexcept override {
    const auto it = symbols_.find(std::string(name));
    return it == symbols_.end() ? nullptr : it->second;
  }
  void remove(const std::string& name) { symbols_.erase(name); }

  static std::unique_ptr<FakeLibrary> complete() {
    auto lib = std::make_unique<FakeLibrary>();
    lib->symbols_ = {
        {"nvmlInit_v2", reinterpret_cast<void*>(&fakeInit)},
        {"nvmlShutdown", reinterpret_cast<void*>(&fakeShutdown)},
        {"nvmlDeviceGetCount_v2", reinterpret_cast<void*>(&fakeCount)},
        {"nvmlDeviceGetHandleByIndex_v2", reinterpret_cast<void*>(&fakeHandle)},
        {"nvmlDeviceGetName", reinterpret_cast<void*>(&fakeName)},
        {"nvmlDeviceGetMemoryInfo", reinterpret_cast<void*>(&fakeMemory)},
        {"nvmlDeviceGetCudaComputeCapability", reinterpret_cast<void*>(&fakeCompute)},
        {"nvmlSystemGetDriverVersion", reinterpret_cast<void*>(&fakeDriverVersion)},
        {"nvmlSystemGetCudaDriverVersion", reinterpret_cast<void*>(&fakeCudaVersion)},
    };
    return lib;
  }

 private:
  std::map<std::string, void*> symbols_;
};

ai::NvmlGpuProbe probeWith(std::unique_ptr<FakeLibrary> library, std::vector<std::string> names = {"nvml.dll"}) {
  auto shared = std::shared_ptr<FakeLibrary>(std::move(library));
  // The loader hands out a non-owning view so the probe can open the "library" without owning the test's fake.
  struct View final : DynamicLibrary {
    explicit View(std::shared_ptr<FakeLibrary> l) : lib(std::move(l)) {}
    void* findSymbol(std::string_view name) const noexcept override { return lib->findSymbol(name); }
    std::shared_ptr<FakeLibrary> lib;
  };
  return ai::NvmlGpuProbe(
      [shared](std::string_view) -> std::unique_ptr<DynamicLibrary> {
        return shared ? std::make_unique<View>(shared) : nullptr;
      },
      std::move(names));
}

void resetFake() {
  g = FakeNvml{};
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("no NVML on the machine reports NoDriver, not an error") {
  resetFake();
  ai::NvmlGpuProbe probe([](std::string_view) { return std::unique_ptr<DynamicLibrary>{}; },
                         {"nvml.dll", "libnvidia-ml.so.1"});

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::NoDriver);
  CHECK(info.devices.empty());
  CHECK(contains(info.detail, "nvml.dll"));  // says what was looked for
  CHECK(g.initCalls == 0);
}

TEST_CASE("two GPUs are listed with memory, compute capability and driver details") {
  resetFake();
  auto probe = probeWith(FakeLibrary::complete());

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::Available);
  CHECK(info.vendor == "NVIDIA");
  CHECK(info.driverVersion == "576.52");
  CHECK(info.computeApi == "CUDA 13.4");
  REQUIRE(info.devices.size() == 2U);
  CHECK(info.devices[0].index == 0);
  CHECK(info.devices[0].name == "NVIDIA GeForce RTX 3090");
  CHECK(info.devices[1].name == "NVIDIA GeForce RTX 3090 Ti");
  CHECK(info.devices[0].vramTotalBytes == 24ULL * 1024 * 1024 * 1024);
  CHECK(info.devices[0].vramFreeBytes == 20ULL * 1024 * 1024 * 1024);
  CHECK(info.devices[0].computeCapability == "8.6");
}

TEST_CASE("NVML is initialised once and always shut down") {
  resetFake();
  auto probe = probeWith(FakeLibrary::complete());

  (void)probe.probe();
  CHECK(g.initCalls == 1);
  CHECK(g.shutdownCalls == 1);

  resetFake();
  g.deviceCount = 0;
  auto empty = probeWith(FakeLibrary::complete());
  const GpuInfo info = empty.probe();
  CHECK(info.status == GpuInfo::Status::NoDevices);
  CHECK(g.shutdownCalls == 1);
}

TEST_CASE("the first library name that loads is used") {
  resetFake();
  auto library = std::shared_ptr<FakeLibrary>(FakeLibrary::complete());
  std::vector<std::string> tried;
  struct View final : DynamicLibrary {
    explicit View(std::shared_ptr<FakeLibrary> l) : lib(std::move(l)) {}
    void* findSymbol(std::string_view name) const noexcept override { return lib->findSymbol(name); }
    std::shared_ptr<FakeLibrary> lib;
  };
  ai::NvmlGpuProbe probe(
      [&](std::string_view name) -> std::unique_ptr<DynamicLibrary> {
        tried.emplace_back(name);
        return name == "second.so" ? std::make_unique<View>(library) : nullptr;
      },
      {"first.so", "second.so", "third.so"});

  CHECK(probe.probe().status == GpuInfo::Status::Available);
  CHECK(tried == std::vector<std::string>{"first.so", "second.so"});
}

TEST_CASE("a driver that is not loaded is NoDriver; any other init failure is an Error with the code") {
  resetFake();
  g.initResult = kDriverNotLoaded;
  auto notLoaded = probeWith(FakeLibrary::complete());
  CHECK(notLoaded.probe().status == GpuInfo::Status::NoDriver);

  resetFake();
  g.initResult = kUnknown;
  auto broken = probeWith(FakeLibrary::complete());
  const GpuInfo info = broken.probe();
  CHECK(info.status == GpuInfo::Status::Error);
  CHECK(contains(info.detail, "999"));
  CHECK(g.shutdownCalls == 0);  // never initialised, so nothing to shut down
}

TEST_CASE("a library missing a required symbol is an Error naming the symbol") {
  resetFake();
  auto library = FakeLibrary::complete();
  library->remove("nvmlDeviceGetMemoryInfo");
  auto probe = probeWith(std::move(library));

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::Error);
  CHECK(contains(info.detail, "nvmlDeviceGetMemoryInfo"));
  CHECK(g.initCalls == 0);
}

TEST_CASE("older drivers without the optional queries still list the devices") {
  resetFake();
  auto library = FakeLibrary::complete();
  library->remove("nvmlDeviceGetCudaComputeCapability");
  library->remove("nvmlSystemGetCudaDriverVersion");
  library->remove("nvmlSystemGetDriverVersion");
  auto probe = probeWith(std::move(library));

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::Available);
  CHECK(info.computeApi.empty());
  CHECK(info.driverVersion.empty());
  REQUIRE(info.devices.size() == 2U);
  CHECK(info.devices[0].computeCapability.empty());
}

TEST_CASE("a device whose name cannot be read is still listed") {
  resetFake();
  g.nameResult = kUnknown;
  auto probe = probeWith(FakeLibrary::complete());

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::Available);
  REQUIRE(info.devices.size() == 2U);
  CHECK(info.devices[0].name == "unknown");
}

TEST_CASE("formatCudaVersion decodes NVML's 1000*major + 10*minor encoding") {
  CHECK(ai::formatCudaVersion(13040) == "13.4");
  CHECK(ai::formatCudaVersion(12080) == "12.8");
  CHECK(ai::formatCudaVersion(11000) == "11.0");
  CHECK(ai::formatCudaVersion(0) == "");
  CHECK(ai::formatCudaVersion(-5) == "");
}

TEST_CASE("a failing device count is an Error that keeps the driver details and still shuts NVML down") {
  resetFake();
  g.countResult = kUnknown;
  auto probe = probeWith(FakeLibrary::complete());

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::Error);
  CHECK(contains(info.detail, "999"));
  CHECK(info.driverVersion == "576.52");  // read before the failure, not thrown away
  CHECK(g.initCalls == 1);
  CHECK(g.shutdownCalls == 1);
}

TEST_CASE("when no device can be opened the result is an Error, not NoDevices") {
  resetFake();
  g.handleResult = kUnknown;
  auto probe = probeWith(FakeLibrary::complete());

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::Error);
  CHECK(info.devices.empty());
  CHECK(contains(info.detail, "not accessible"));
  CHECK(g.shutdownCalls == 1);
}

TEST_CASE("a partial failure lists the working devices and explains the missing one") {
  resetFake();
  g.failHandleFrom = 1;  // device 0 works, device 1 does not
  auto probe = probeWith(FakeLibrary::complete());

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::Available);
  REQUIRE(info.devices.size() == 1U);
  CHECK(info.devices[0].index == 0);
  CHECK(contains(info.detail, "device 1"));  // surfaced to the report as a note
}

TEST_CASE("unreadable memory is reported as unknown (0) with a note, not as an empty GPU") {
  resetFake();
  g.memoryResult = kUnknown;
  auto probe = probeWith(FakeLibrary::complete());

  const GpuInfo info = probe.probe();

  CHECK(info.status == GpuInfo::Status::Available);
  REQUIRE(info.devices.size() == 2U);
  CHECK(info.devices[0].vramTotalBytes == 0U);
  CHECK(contains(info.detail, "memory"));
}

TEST_CASE("a name longer than NVML's buffer is truncated and terminated, never overrun") {
  resetFake();
  g.names = {std::string(500, 'x'), "short"};
  auto probe = probeWith(FakeLibrary::complete());

  const GpuInfo info = probe.probe();

  REQUIRE(info.devices.size() == 2U);
  CHECK(info.devices[0].name.size() == 95U);  // 96-byte buffer minus the terminator
  CHECK(info.devices[1].name == "short");
}

TEST_CASE("strings from a library that forgets the terminator are cut at the buffer size") {
  resetFake();
  g.noTerminator = true;
  auto probe = probeWith(FakeLibrary::complete());

  const GpuInfo info = probe.probe();

  REQUIRE(info.devices.size() == 2U);
  CHECK(info.devices[0].name.size() == 95U);  // 96-byte buffer, last byte forced to NUL
  CHECK(info.driverVersion.size() == 79U);    // 80-byte buffer
}

// Real hardware: runs against the actual NVIDIA driver when there is one, otherwise skips (CI has no GPU).
TEST_CASE("the real NVML reports plausible devices when a driver is installed", "[hardware]") {
  ai::NvmlGpuProbe probe(openDynamicLibrary, platform::nvmlLibraryNames());

  const GpuInfo info = probe.probe();

  if (info.status != GpuInfo::Status::Available) {
    // No driver, a driver without GPUs, or a broken/mismatched driver: all environment problems, not ours to fail on.
    SKIP("no usable NVIDIA GPU on this machine (" << info.detail << ")");
  }
  INFO("status detail: " << info.detail);
  REQUIRE(info.status == GpuInfo::Status::Available);
  CHECK_FALSE(info.devices.empty());
  for (const GpuDevice& device : info.devices) {
    INFO("device " << device.index << " " << device.name);
    CHECK_FALSE(device.name.empty());
    CHECK(device.vramTotalBytes > 0U);
    CHECK(device.vramFreeBytes <= device.vramTotalBytes);
  }
}
