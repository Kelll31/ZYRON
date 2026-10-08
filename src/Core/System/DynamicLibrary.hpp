// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace zyron::core {

/// A loaded shared library (DLL / .so / .dylib). Used to reach optional system libraries - such as NVIDIA's NVML -
/// without linking them, so ZYRON starts and works on machines that do not have them (SPEC sections 37, 40).
class DynamicLibrary {
 public:
  virtual ~DynamicLibrary() = default;

  /// Address of an exported symbol, or nullptr. The caller casts it to the function type it expects.
  [[nodiscard]] virtual void* findSymbol(std::string_view name) const noexcept = 0;
};

/// Injectable library opener; production code passes openDynamicLibrary, tests pass a fake.
using LibraryLoader = std::function<std::unique_ptr<DynamicLibrary>(std::string_view libraryName)>;

// Declared here, defined once per OS in src/Platform (selected by CMake, never by #ifdef in shared code - SPEC 78).

/// Loads a shared library by file name; nullptr when it is missing or fails to load (the two cases are not
/// distinguished - the reason is not exposed). Never throws.
///
/// Security (SPEC section 76): the name must come from code, never from the UI, a file, the network or the AI -
/// loading a library runs its initialisation code. On Windows a bare name is looked up in System32 only.
[[nodiscard]] std::unique_ptr<DynamicLibrary> openDynamicLibrary(std::string_view libraryName);
}  // namespace zyron::core
