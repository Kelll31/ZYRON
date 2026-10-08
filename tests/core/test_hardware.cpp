// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>

#include "Core/System/EngineStats.hpp"
#include "Core/System/HardwareProbe.hpp"

using namespace zyron::core;

namespace {

constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

struct FakeSystemProbe final : SystemProbe {
  SystemInfo probe() override {
    SystemInfo info;
    info.os = {"Windows 11", "64-bit"};
    info.cpu = {"Test CPU 9000", 16, 8};
    info.memory.totalBytes = 64 * kGiB;
    return info;
  }
};

struct FakeGpuProbe final : GpuProbe {
  GpuInfo probe() override {
    GpuInfo info;
    info.status = GpuInfo::Status::Available;
    info.vendor = "NVIDIA";
    info.driverVersion = "576.52";
    info.computeApi = "CUDA 13.4";
    info.devices = {{0, "RTX 3090", 24 * kGiB, 23 * kGiB, "8.6"}, {1, "RTX 3090", 24 * kGiB, 24 * kGiB, "8.6"}};
    return info;
  }
};

struct FakeAudioProbe final : AudioDeviceProbe {
  std::vector<AudioDeviceInfo> probe() override {
    AudioDeviceInfo device;
    device.apiName = "Windows Audio";
    device.name = "Speakers";
    device.isOutput = true;
    device.isDefaultOutput = true;
    device.capabilitiesKnown = true;
    device.outputChannels = 2;
    device.sampleRates = {44100.0, 48000.0};
    device.bufferSizes = {128, 256};
    device.defaultBufferSize = 256;
    return {device};
  }
};

struct FakeMidiProbe final : MidiProbe {
  MidiDevices probe() override { return {{{"Launchpad", "id-in-1"}}, {{"Launchpad", "id-out-1"}}}; }
};

struct ThrowingGpuProbe final : GpuProbe {
  GpuInfo probe() override { throw std::runtime_error("nvml exploded"); }
};

struct ThrowingAudioProbe final : AudioDeviceProbe {
  std::vector<AudioDeviceInfo> probe() override { throw std::runtime_error("asio hung"); }
};

HardwareDetector::Probes allFakes() {
  return {std::make_shared<FakeSystemProbe>(), std::make_shared<FakeGpuProbe>(), std::make_shared<FakeAudioProbe>(),
          std::make_shared<FakeMidiProbe>()};
}

}  // namespace

TEST_CASE("formatBytes uses binary units and is locale independent") {
  CHECK(formatBytes(0) == "0 B");
  CHECK(formatBytes(512) == "512 B");
  CHECK(formatBytes(1536) == "1.5 KiB");
  CHECK(formatBytes(24 * kGiB) == "24.0 GiB");
  CHECK(formatBytes(1536ULL * 1024ULL * 1024ULL) == "1.5 GiB");
  CHECK(formatBytes(3 * 1024ULL * kGiB) == "3.0 TiB");
}

TEST_CASE("formatBytes never prints a value that rounds up to the next unit's threshold") {
  CHECK(formatBytes(1048575) == "1.0 MiB");  // 1023.999 KiB would otherwise print as "1024.0 KiB"
  CHECK(formatBytes(1024ULL * kGiB - 1) == "1.0 TiB");
  CHECK(formatBytes(1023) == "1023 B");
}

TEST_CASE("the detector assembles every section from its probes") {
  const HardwareReport report = HardwareDetector{allFakes()}.detect();

  CHECK(report.system.os.name == "Windows 11");
  CHECK(report.system.cpu.logicalCores == 16);
  CHECK(report.system.memory.totalBytes == 64 * kGiB);
  CHECK(report.gpu.status == GpuInfo::Status::Available);
  REQUIRE(report.gpu.devices.size() == 2U);
  CHECK(report.gpu.devices[1].index == 1);
  REQUIRE(report.audioDevices.size() == 1U);
  CHECK(report.audioDevices.front().name == "Speakers");
  REQUIRE(report.midiInputs.size() == 1U);
  REQUIRE(report.midiOutputs.size() == 1U);
  CHECK(report.midiOutputs.front().identifier == "id-out-1");
  CHECK(report.warnings.empty());
}

TEST_CASE("missing probes leave their sections empty instead of failing") {
  const HardwareReport report = HardwareDetector{HardwareDetector::Probes{}}.detect();

  CHECK(report.system.os.name.empty());
  CHECK(report.gpu.status == GpuInfo::Status::NotProbed);
  CHECK(report.audioDevices.empty());
  CHECK(report.midiInputs.empty());
  CHECK(report.warnings.empty());
}

TEST_CASE("a probe that throws is isolated and the other sections survive") {
  auto probes = allFakes();
  probes.gpu = std::make_shared<ThrowingGpuProbe>();
  probes.audio = std::make_shared<ThrowingAudioProbe>();

  HardwareReport report;
  REQUIRE_NOTHROW(report = HardwareDetector{probes}.detect());

  CHECK(report.gpu.status == GpuInfo::Status::Error);
  CHECK(contains(report.gpu.detail, "nvml exploded"));
  CHECK(report.audioDevices.empty());
  REQUIRE(report.warnings.size() == 2U);  // GPU and audio each recorded
  CHECK(contains(report.warnings[1], "asio hung"));

  // untouched sections
  CHECK(report.system.os.name == "Windows 11");
  CHECK(report.midiInputs.size() == 1U);
}

TEST_CASE("formatReport lists system, GPUs, audio and MIDI") {
  const std::string text = formatReport(HardwareDetector{allFakes()}.detect());

  CHECK(contains(text, "Windows 11"));
  CHECK(contains(text, "64-bit"));
  CHECK(contains(text, "Test CPU 9000"));
  CHECK(contains(text, "16 logical"));
  CHECK(contains(text, "8 physical"));
  CHECK(contains(text, "64.0 GiB"));
  CHECK(contains(text, "2 NVIDIA GPU(s)"));
  CHECK(contains(text, "driver 576.52"));
  CHECK(contains(text, "CUDA 13.4"));
  CHECK(contains(text, "[0] RTX 3090"));
  CHECK(contains(text, "[1] RTX 3090"));
  CHECK(contains(text, "24.0 GiB"));
  CHECK(contains(text, "compute 8.6"));
  CHECK(contains(text, "Windows Audio"));
  CHECK(contains(text, "Speakers"));
  CHECK(contains(text, "48000"));
  CHECK(contains(text, "Launchpad"));
}

TEST_CASE("formatReport summarises a long list of buffer sizes as a range") {
  HardwareReport report;
  AudioDeviceInfo device;
  device.apiName = "ASIO";
  device.name = "Interface";
  device.isOutput = true;
  device.capabilitiesKnown = true;
  device.outputChannels = 4;
  device.bufferSizes = {64, 128, 192, 256, 512, 1024, 2048};
  device.defaultBufferSize = 256;
  report.audioDevices = {device};

  const std::string text = formatReport(report);

  CHECK(contains(text, "buffers 64-2048 (7 sizes, default 256)"));
  CHECK_FALSE(contains(text, "192"));  // the individual sizes are not listed
}

TEST_CASE("a quick scan lists direction and defaults without inventing capabilities") {
  HardwareReport report;
  AudioDeviceInfo output;
  output.apiName = "Windows Audio";
  output.name = "Speakers";
  output.isOutput = true;
  output.isDefaultOutput = true;
  AudioDeviceInfo duplex;
  duplex.apiName = "ASIO";
  duplex.name = "Interface";
  duplex.isInput = true;
  duplex.isOutput = true;
  report.audioDevices = {output, duplex};

  const std::string text = formatReport(report);

  CHECK(contains(text, "Windows Audio | Speakers | out (default)"));
  CHECK(contains(text, "ASIO | Interface | out in"));
  for (char digit = '0'; digit <= '9'; ++digit) {
    CHECK_FALSE(contains(text, std::string(1, digit) + "ch"));  // no channel counts were measured
  }
  CHECK_FALSE(contains(text, " Hz"));
  CHECK_FALSE(contains(text, "buffers"));
}

TEST_CASE("formatReport passes non-ASCII device names through unchanged") {
  HardwareReport report;
  AudioDeviceInfo device;
  device.apiName = "Windows Audio";
  device.name = "\xD0\x94\xD0\xB8\xD0\xBD\xD0\xB0\xD0\xBC\xD0\xB8\xD0\xBA\xD0\xB8";  // "Динамики" in UTF-8
  device.isOutput = true;
  report.audioDevices = {device};

  CHECK(contains(formatReport(report), device.name));
}

TEST_CASE("formatReport shows unknown VRAM, unknown compute capability and non-fatal GPU notes") {
  HardwareReport report;
  report.gpu.status = GpuInfo::Status::Available;
  report.gpu.vendor = "NVIDIA";
  report.gpu.detail = "device 1 not accessible";
  report.gpu.devices = {{0, "Mystery GPU", 0, 0, ""}};

  const std::string text = formatReport(report);

  CHECK(contains(text, "[0] Mystery GPU"));
  CHECK(contains(text, "VRAM unknown"));
  CHECK(contains(text, "compute unknown"));
  CHECK(contains(text, "note: device 1 not accessible"));
}

TEST_CASE("formatReport explains an unavailable GPU and empty device lists") {
  HardwareReport report;
  report.gpu.status = GpuInfo::Status::NoDriver;
  report.gpu.detail = "NVIDIA management library not found";
  report.warnings.emplace_back("audio probe failed: asio hung");

  const std::string text = formatReport(report);

  CHECK(contains(text, "no supported GPU available"));
  CHECK(contains(text, "NVIDIA management library not found"));
  CHECK(contains(text, "CPU backend"));
  CHECK(contains(text, "Audio devices: none"));
  CHECK(contains(text, "MIDI inputs  : none"));
  CHECK(contains(text, "MIDI outputs : none"));
  CHECK(contains(text, "audio probe failed: asio hung"));
}

TEST_CASE("engine stats summarise the open device") {
  AudioEngineStats stats;
  stats.deviceOpen = true;
  stats.apiName = "Windows Audio";
  stats.deviceName = "Speakers";
  stats.sampleRate = 48000.0;
  stats.bufferSize = 480;
  stats.outputChannels = 2;
  stats.outputLatencyMs = 21.3;
  stats.callbackCount = 1234;
  stats.xrunCount = 0;
  stats.cpuLoad = 0.125;

  const std::string text = formatEngineStats(stats);

  CHECK(contains(text, "Windows Audio / Speakers"));
  CHECK(contains(text, "48000 Hz, 480 frames (10.0 ms), 2 out"));
  CHECK(contains(text, "21.3 ms output"));
  CHECK(contains(text, "Callbacks : 1234"));
  CHECK(contains(text, "Xruns     : 0"));
  CHECK(contains(text, "DSP load  : 12.5 %"));
  CHECK_FALSE(contains(text, "Error"));
}

TEST_CASE("engine stats say when xruns are not reported and when no device is open") {
  AudioEngineStats open;
  open.deviceOpen = true;
  open.sampleRate = 44100.0;
  open.bufferSize = 256;
  open.xrunCount = -1;
  CHECK(contains(formatEngineStats(open), "n/a"));

  AudioEngineStats closed;
  closed.lastError = "device busy";
  const std::string text = formatEngineStats(closed);
  CHECK(contains(text, "No audio device open"));
  CHECK(contains(text, "Error: device busy"));
}

TEST_CASE("the report framing is plain ASCII (only device names may carry UTF-8)") {
  const std::string text = formatReport(HardwareDetector{allFakes()}.detect());
  for (const char c : text) {
    CHECK((c == '\n' || (c >= 0x20 && c < 0x7f)));
  }
}
