# SPDX-License-Identifier: AGPL-3.0-only
#
# Pinned third-party dependencies (ADR-0004). Heavy binary deps (FFmpeg, SQLite, ONNX Runtime) will come from
# vcpkg manifest mode when the first one lands. Every entry here must also be listed in THIRD_PARTY_NOTICES.md.
include_guard(GLOBAL)
include(FetchContent)

# JUCE (AGPLv3 option of its dual licence, ADR-0002) - GUI, audio devices, MIDI. Pulled in only when the app is built.
macro(zyron_require_juce)
  if(NOT TARGET juce::juce_gui_basics)
    FetchContent_Declare(JUCE
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG        9.0.3
      GIT_SHALLOW    TRUE
      SYSTEM)
    FetchContent_MakeAvailable(JUCE)

    # One JUCE configuration for every target that compiles JUCE module sources (they are INTERFACE sources, so each
    # linking target gets its own copy): differing definitions between copies would be an ODR violation.
    add_library(zyron_juce_config INTERFACE)
    add_library(zyron::juce_config ALIAS zyron_juce_config)
    target_compile_definitions(zyron_juce_config INTERFACE
      JUCE_WEB_BROWSER=0   # no embedded browser: avoids a WebKit/WebView2 dependency
      JUCE_USE_CURL=0)     # no libcurl: avoids a Linux dev-package dependency and any hidden network code (SPEC 75)
  endif()
endmacro()

# Catch2 (BSL-1.0) - unit tests only, never shipped.
macro(zyron_require_catch2)
  if(NOT TARGET Catch2::Catch2WithMain)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG        v3.16.0
      GIT_SHALLOW    TRUE
      SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
  endif()
endmacro()

# SQLite3 (Public domain) - Database for tracks, playlists, metadata, analysis (SPEC section 28, ROADMAP P3-01).
macro(zyron_require_sqlite3)
  if(NOT TARGET zyron_sqlite3)
    if(DEFINED ENV{VCPKG_ROOT})
      list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
    elseif(EXISTS "C:/Users/User/vcpkg/installed/x64-windows")
      list(APPEND CMAKE_PREFIX_PATH "C:/Users/User/vcpkg/installed/x64-windows")
    endif()

    find_package(unofficial-sqlite3 CONFIG QUIET)
    if(TARGET unofficial::sqlite3::sqlite3)
      add_library(zyron_sqlite3 INTERFACE)
      target_link_libraries(zyron_sqlite3 INTERFACE unofficial::sqlite3::sqlite3)
    else()
      find_package(SQLite3 REQUIRED)
      add_library(zyron_sqlite3 INTERFACE)
      target_link_libraries(zyron_sqlite3 INTERFACE SQLite::SQLite3)
    endif()
    add_library(zyron::sqlite3 ALIAS zyron_sqlite3)
  endif()
endmacro()
