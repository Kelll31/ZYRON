# SPDX-License-Identifier: AGPL-3.0-only
#
# zyron_target_defaults(<target>)
#   Applies the project-wide language level and warning policy to one of OUR targets.
#   Warnings-as-errors follows ZYRON_WARNINGS_AS_ERRORS.
#
#   The warning flags are attached to the target's own source files, not to the target: JUCE's module sources are
#   INTERFACE sources that CMake compiles *into* every target that links a JUCE module, and third-party code must not
#   be held to our policy (JUCE builds with a different, looser GCC flag set; with -Wconversion -Werror it would not
#   compile). Call this after the target's sources have been added.
function(zyron_target_defaults target)
  target_compile_features(${target} PUBLIC cxx_std_20)

  if(MSVC)
    # /Zc:__cplusplus makes __cplusplus report the real standard (MSVC says 199711 otherwise).
    target_compile_options(${target} PUBLIC /permissive- /utf-8 /Zc:__cplusplus)
    # /external:W0 keeps third-party (SYSTEM) headers such as JUCE's from tripping /W4.
    set(_zyron_flags /W4 /external:W0)
    if(ZYRON_WARNINGS_AS_ERRORS)
      list(APPEND _zyron_flags /WX)
    endif()
  else()
    set(_zyron_flags -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(ZYRON_WARNINGS_AS_ERRORS)
      list(APPEND _zyron_flags -Werror)
    endif()
  endif()

  get_target_property(_zyron_sources ${target} SOURCES)
  set(_zyron_own_sources "")
  foreach(_source IN LISTS _zyron_sources)
    if(_source MATCHES "\\.(cpp|cc|cxx|c)$")
      list(APPEND _zyron_own_sources "${_source}")
    endif()
  endforeach()
  if(_zyron_own_sources)
    set_source_files_properties(${_zyron_own_sources} PROPERTIES COMPILE_OPTIONS "${_zyron_flags}")
  endif()
endfunction()
