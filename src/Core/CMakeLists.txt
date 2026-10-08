# SPDX-License-Identifier: AGPL-3.0-only
#
# zyron_core: Command API, application state, events. std-only: no JUCE, no OS APIs, no GPU libraries
# (CLAUDE.md non-negotiable #4, ADR-0003).
find_package(Threads REQUIRED)

add_library(zyron_core STATIC
  Commands/Command.cpp
  Commands/CommandBus.cpp
  Events/EventBus.cpp
  State/AppState.cpp
  System/EngineStats.cpp
  System/HardwareInfo.cpp
  Audio/WaveformData.cpp)
add_library(zyron::core ALIAS zyron_core)

# Public include root is src/, so headers are included as "Core/Commands/Command.hpp".
target_include_directories(zyron_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_core PUBLIC Threads::Threads)
zyron_target_defaults(zyron_core)
