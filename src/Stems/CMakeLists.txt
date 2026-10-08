# SPDX-License-Identifier: AGPL-3.0-only
#
# zyron_stems: AI stem separation interface, implementations, and cache (SPEC sections 32, 42).
# JUCE-free; GPU libraries are accessed only through AI backends.
add_library(zyron_stems STATIC
  StemTypes.cpp
  MockStemSeparator.cpp
  Demucs/DemucsStemSeparator.cpp
  Cache/StemCache.cpp)
add_library(zyron::stems ALIAS zyron_stems)

target_include_directories(zyron_stems PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_stems PUBLIC zyron::core zyron::ai)
zyron_target_defaults(zyron_stems)
