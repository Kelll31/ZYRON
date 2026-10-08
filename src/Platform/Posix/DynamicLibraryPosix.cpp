// SPDX-License-Identifier: AGPL-3.0-only
#include <dlfcn.h>

#include <cstring>
#include <string>

#include "Core/System/DynamicLibrary.hpp"

namespace zyron::core {
namespace {

constexpr std::size_t kMaxSymbolLength = 255;

class PosixLibrary final : public DynamicLibrary {
 public:
  explicit PosixLibrary(void* handle) : handle_(handle) {}
  ~PosixLibrary() override { dlclose(handle_); }
  PosixLibrary(const PosixLibrary&) = delete;
  PosixLibrary& operator=(const PosixLibrary&) = delete;

  /// Copies the name to a stack buffer: this is noexcept, so it must not allocate.
  [[nodiscard]] void* findSymbol(std::string_view name) const noexcept override {
    if (name.empty() || name.size() > kMaxSymbolLength) {
      return nullptr;
    }
    char terminated[kMaxSymbolLength + 1];
    std::memcpy(terminated, name.data(), name.size());
    terminated[name.size()] = '\0';
    return dlsym(handle_, terminated);
  }

 private:
  void* handle_;
};

}  // namespace

std::unique_ptr<DynamicLibrary> openDynamicLibrary(std::string_view libraryName) {
  if (libraryName.empty()) {
    return nullptr;
  }
  try {
    const std::string terminated(libraryName);
    void* handle = dlopen(terminated.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
      return nullptr;
    }
    try {
      return std::make_unique<PosixLibrary>(handle);
    } catch (...) {
      dlclose(handle);  // allocation failed after the load: do not leak the handle
      throw;
    }
  } catch (...) {
    return nullptr;  // "never throws" (DynamicLibrary.hpp): out of memory counts as "could not load"
  }
}

}  // namespace zyron::core
