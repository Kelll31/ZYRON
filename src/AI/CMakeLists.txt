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
  Models/ModelManager.cpp
  Embedding/EmbeddingPluginService.cpp
  Recommendation/CompatibilityScorer.cpp
  Recommendation/TrackRecommender.cpp
  Recommendation/SemanticSearch.cpp
  SetBuilder/SetBuilder.cpp
  Timeline/CommandTimelineScheduler.cpp
  Transition/TransitionPlanner.cpp
  Autonomous/AutonomousDjLoop.cpp
  LLM/LocalLlmAdapter.cpp
  Safety/AiSafetyController.cpp)
add_library(zyron::ai ALIAS zyron_ai)

if(TARGET zyron::onnxruntime)
  target_sources(zyron_ai PRIVATE Backends/Onnx/OnnxRuntime.cpp)
  target_link_libraries(zyron_ai PUBLIC zyron::onnxruntime)
  target_compile_definitions(zyron_ai PUBLIC ZYRON_HAS_ONNX=1)
endif()

target_include_directories(zyron_ai PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_ai PUBLIC zyron::core)
zyron_target_defaults(zyron_ai)
