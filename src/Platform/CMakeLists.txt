# SPDX-License-Identifier: AGPL-3.0-only
#
# OS-specific code lives only here, behind interfaces declared in Core (SPEC section 78, CLAUDE.md non-negotiable #4).
# The right implementation is chosen by CMake, never by #ifdef in shared code.
add_library(zyron_platform STATIC)
add_library(zyron::platform ALIAS zyron_platform)

target_sources(zyron_platform PRIVATE
  Common/BaseFileSystem.cpp
  Release/UpdateChecker.cpp
  Release/FirstRunManager.cpp)

if(WIN32)
  target_sources(zyron_platform PRIVATE
    Windows/DynamicLibraryWin.cpp
    Windows/WindowsFileSystem.cpp
    Windows/WindowsPlatformWindow.cpp)
else()
  target_sources(zyron_platform PRIVATE Posix/DynamicLibraryPosix.cpp)
  target_link_libraries(zyron_platform PRIVATE ${CMAKE_DL_LIBS})
  if(APPLE)
    target_sources(zyron_platform PRIVATE
      MacOS/GpuLibraryNames.cpp
      MacOS/MacOsFileSystem.cpp
      MacOS/MacOsPlatformWindow.cpp)
  else()
    target_sources(zyron_platform PRIVATE
      Linux/GpuLibraryNames.cpp
      Linux/LinuxFileSystem.cpp
      Linux/LinuxPlatformWindow.cpp)
  endif()
endif()

target_include_directories(zyron_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_platform PUBLIC zyron::core)
zyron_target_defaults(zyron_platform)

# OS-agnostic probes built on JUCE (needs the JUCE dependency, so only with the app).
if(ZYRON_BUILD_APP)
  add_library(zyron_platform_juce STATIC Common/JuceSystemProbe.cpp)
  add_library(zyron::platform_juce ALIAS zyron_platform_juce)
  target_include_directories(zyron_platform_juce PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
  # JuceSystemProbe.hpp is JUCE-free, so JUCE stays an implementation detail of this library.
  target_link_libraries(zyron_platform_juce
    PUBLIC  zyron::core
    PRIVATE zyron::juce_config juce::juce_core juce::juce_recommended_config_flags)
  zyron_target_defaults(zyron_platform_juce)
endif()
