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
    # Windows: prefer the static-md triplet (SQLite linked into the exe, dynamic CRT) so no sqlite3.dll has to ship
    # next to the binary; fall back to the DLL triplet.
    if(DEFINED ENV{VCPKG_ROOT})
      set(_zyron_vcpkg_root "$ENV{VCPKG_ROOT}")
    elseif(EXISTS "C:/Users/User/vcpkg")
      set(_zyron_vcpkg_root "C:/Users/User/vcpkg")
    endif()
    if(_zyron_vcpkg_root)
      foreach(_triplet x64-windows-static-md x64-windows)
        if(EXISTS "${_zyron_vcpkg_root}/installed/${_triplet}")
          list(APPEND CMAKE_PREFIX_PATH "${_zyron_vcpkg_root}/installed/${_triplet}")
          break()
        endif()
      endforeach()
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

# Signalsmith Stretch (MIT) + Signalsmith Linear (MIT, its FFT/STFT) - header-only time-stretch / pitch-shift engine for
# keylock (master tempo) and key shift (ADR-0018). Both are pinned to commit SHAs (tags 1.4.0 and 0.6.4). We do NOT run
# their CMakeLists (SOURCE_SUBDIR points nowhere): it would add global compile options; we only need the include paths.
macro(zyron_require_signalsmith_stretch)
  if(NOT TARGET zyron_signalsmith_stretch)
    FetchContent_Declare(signalsmith_linear
      GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
      GIT_TAG        de55e6a50ffcf6f8f43f649692d94691c7025151  # 0.6.4
      GIT_SHALLOW    FALSE
      SOURCE_SUBDIR  zyron_headers_only
      SYSTEM)
    FetchContent_Declare(signalsmith_stretch
      GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
      GIT_TAG        a670068d9aeb64913331d5cc29337b19a457a7df  # 1.4.0
      GIT_SHALLOW    FALSE
      SOURCE_SUBDIR  zyron_headers_only
      SYSTEM)
    FetchContent_MakeAvailable(signalsmith_linear signalsmith_stretch)

    # One-word robustness patch (idempotent, skipped if upstream changed the line): the library keeps its spectral peaks
    # in a vector reserved for bands/2 entries, but a spectrum of alternating peaks over an odd number of bands has one
    # more. That would be a heap allocation on the audio thread, so reserve two more.
    set(_zyron_stretch_header "${signalsmith_stretch_SOURCE_DIR}/signalsmith-stretch.h")
    file(READ "${_zyron_stretch_header}" _zyron_stretch_text)
    string(FIND "${_zyron_stretch_text}" "peaks.reserve(bands/2);" _zyron_stretch_pos)
    if(NOT _zyron_stretch_pos EQUAL -1)
      string(REPLACE "peaks.reserve(bands/2);" "peaks.reserve(bands/2 + 2);" _zyron_stretch_text "${_zyron_stretch_text}")
      file(WRITE "${_zyron_stretch_header}" "${_zyron_stretch_text}")
    endif()

    add_library(zyron_signalsmith_stretch INTERFACE)
    add_library(zyron::signalsmith_stretch ALIAS zyron_signalsmith_stretch)
    target_include_directories(zyron_signalsmith_stretch SYSTEM INTERFACE
      "${signalsmith_stretch_SOURCE_DIR}/include"
      "${signalsmith_linear_SOURCE_DIR}/include")
  endif()
endmacro()
