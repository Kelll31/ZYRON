// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "Core/System/DynamicLibrary.hpp"
#include "Platform/GpuLibraries.hpp"

using namespace zyron::core;

// ZYRON_TEST_LIBRARY / ZYRON_TEST_SYMBOL / ZYRON_TEST_NVML_NAME come from tests/CMakeLists.txt (one system library
// that exists on every machine of the OS under test).

TEST_CASE("an existing system library loads and exposes its symbols") {
  const auto library = openDynamicLibrary(ZYRON_TEST_LIBRARY);
  REQUIRE(library != nullptr);

  CHECK(library->findSymbol(ZYRON_TEST_SYMBOL) != nullptr);
  CHECK(library->findSymbol("zyron_no_such_symbol_exists") == nullptr);
  CHECK(library->findSymbol("") == nullptr);
}

TEST_CASE("absurd symbol names are rejected without allocating or crashing") {
  const auto library = openDynamicLibrary(ZYRON_TEST_LIBRARY);
  REQUIRE(library != nullptr);

  CHECK(library->findSymbol(std::string(255, 'a')) == nullptr);  // the longest accepted name: just not exported
  CHECK(library->findSymbol(std::string(256, 'a')) == nullptr);  // over the limit
  CHECK(library->findSymbol(std::string(100000, 'a')) == nullptr);
  std::string withNul = "GetTick";
  withNul.push_back(char(0));
  withNul += "Count64";
  CHECK(library->findSymbol(withNul) == nullptr);  // an embedded NUL ends the C name: "GetTick" is not exported
}

TEST_CASE("a missing library yields nullptr instead of throwing") {
  CHECK(openDynamicLibrary("zyron-definitely-not-a-library-12345") == nullptr);
  CHECK(openDynamicLibrary("") == nullptr);
}

TEST_CASE("the NVML candidate names match the OS convention") {
  const std::vector<std::string> names = zyron::platform::nvmlLibraryNames();
  const std::string expected = ZYRON_TEST_NVML_NAME;  // empty on macOS

  if (expected.empty()) {
    CHECK(names.empty());
  } else {
    CHECK(std::find(names.begin(), names.end(), expected) != names.end());
  }
}
