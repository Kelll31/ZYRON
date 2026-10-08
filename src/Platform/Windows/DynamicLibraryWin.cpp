// SPDX-License-Identifier: AGPL-3.0-only
#include <cstring>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Core/System/DynamicLibrary.hpp"
#include "Platform/GpuLibraries.hpp"

namespace zyron::core {
namespace {

constexpr std::size_t kMaxSymbolLength = 255;

class WindowsLibrary final : public DynamicLibrary {
 public:
  explicit WindowsLibrary(HMODULE module) : module_(module) {}
  ~WindowsLibrary() override { FreeLibrary(module_); }
  WindowsLibrary(const WindowsLibrary&) = delete;
  WindowsLibrary& operator=(const WindowsLibrary&) = delete;

  /// Copies the name to a stack buffer: this is noexcept, so it must not allocate.
  [[nodiscard]] void* findSymbol(std::string_view name) const noexcept override {
    if (name.empty() || name.size() > kMaxSymbolLength) {
      return nullptr;
    }
    char terminated[kMaxSymbolLength + 1];
    std::memcpy(terminated, name.data(), name.size());
    terminated[name.size()] = '\0';
    return reinterpret_cast<void*>(GetProcAddress(module_, terminated));
  }

 private:
  HMODULE module_;
};

std::wstring toWide(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int size =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
  return wide;
}

/// Keeps Windows from showing a "missing DLL" message box when a library or one of its dependencies is absent.
/// If the error mode cannot be changed, nothing is restored (we never write a made-up "previous" value).
class QuietLoadErrors {
 public:
  QuietLoadErrors() : changed_(SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previous_) != 0) {}
  ~QuietLoadErrors() {
    if (changed_) {
      SetThreadErrorMode(previous_, nullptr);
    }
  }
  QuietLoadErrors(const QuietLoadErrors&) = delete;
  QuietLoadErrors& operator=(const QuietLoadErrors&) = delete;

 private:
  DWORD previous_{0};
  bool changed_;
};

}  // namespace

std::unique_ptr<DynamicLibrary> openDynamicLibrary(std::string_view libraryName) {
  try {
    const std::wstring wide = toWide(libraryName);
    if (wide.empty()) {
      return nullptr;
    }
    const QuietLoadErrors quiet;
    // Bare names are resolved in System32 only (no current-directory / PATH search: DLL-planting protection);
    // absolute paths are loaded as given.
    HMODULE module = LoadLibraryExW(wide.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
      return nullptr;
    }
    try {
      return std::make_unique<WindowsLibrary>(module);
    } catch (...) {
      FreeLibrary(module);  // allocation failed after the load: do not leak the handle
      throw;
    }
  } catch (...) {
    return nullptr;  // "never throws" (DynamicLibrary.hpp): out of memory counts as "could not load"
  }
}

}  // namespace zyron::core

namespace zyron::platform {
namespace {

std::string programFilesDirectory() {
  char buffer[MAX_PATH];
  const DWORD length = GetEnvironmentVariableA("ProgramW6432", buffer, static_cast<DWORD>(sizeof(buffer)));
  if (length > 0 && length < sizeof(buffer)) {
    return std::string(buffer, length);
  }
  return "C:\\Program Files";
}

}  // namespace

std::vector<std::string> nvmlLibraryNames() {
  // Current drivers install nvml.dll into System32; the NVSMI path is where older drivers put it.
  return {"nvml.dll", programFilesDirectory() + "\\NVIDIA Corporation\\NVSMI\\nvml.dll"};
}

}  // namespace zyron::platform
