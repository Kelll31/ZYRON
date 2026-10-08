# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.25)

project(ZYRON
  VERSION 0.1.0
  DESCRIPTION "Native cross-platform DJ application with stems and local AI"
  LANGUAGES C CXX)  # JUCE needs a C compiler for some of its bundled modules

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ZYRON_BUILD_APP "Build the ZYRON application (fetches JUCE)" ON)
option(ZYRON_ENABLE_TESTS "Build the unit tests (fetches Catch2)" ON)
option(ZYRON_WARNINGS_AS_ERRORS "Treat compiler warnings on ZYRON targets as errors" ON)

include(ZyronWarnings)
include(ZyronDependencies)

if(ZYRON_BUILD_APP)
  zyron_require_juce()
endif()

# One static library per module; link edges mirror the allowed include edges in CLAUDE.md.
add_subdirectory(src/Core)
add_subdirectory(src/Platform)
add_subdirectory(src/AI)
add_subdirectory(src/Audio)
add_subdirectory(src/Recording)
add_subdirectory(src/Library)
add_subdirectory(src/Analysis)
add_subdirectory(src/Stems)

if(ZYRON_BUILD_APP)
  add_subdirectory(src/MIDI)
  add_subdirectory(src/UI)
  add_subdirectory(src/Application)
endif()

if(ZYRON_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(ZYRON_BUILD_APP)
  include(cmake/CPackConfig.cmake)
endif()

