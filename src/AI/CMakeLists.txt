# SPDX-License-Identifier: AGPL-3.0-only
#
# zyron_ai: AI runtime, backends, stem separators. JUCE-free; GPU libraries are reached only from Backends/*, and only
# through run-time loading, so the app starts on machines without them (SPEC sections 37, 40).
add_library(zyron_ai STATIC
  Backends/Cuda/NvmlGpuProbe.cpp
  Backends/Cuda/CudaBackend.cpp
  Backends/Cpu/CpuBackend.cpp
  Runtime/GpuScheduler.cpp
  Runtime/BackgroundQueueManager.cpp
  Models/ModelManifest.cpp
  Models/ModelManager.cpp)
add_library(zyron::ai ALIAS zyron_ai)

target_include_directories(zyron_ai PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_ai PUBLIC zyron::core)
zyron_target_defaults(zyron_ai)
