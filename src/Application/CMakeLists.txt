# SPDX-License-Identifier: AGPL-3.0-only
#
# The composition root: wires the modules together and owns their lifetimes. Nothing links against it.
# The wiring itself is a library so the app-level tests build exactly what Main.cpp runs.
add_library(zyron_application STATIC AnalysisSidecar.cpp ApplicationComposition.cpp AutomixController.cpp NeuralModels.cpp StemService.cpp TrackAnalyzer.cpp)
add_library(zyron::application ALIAS zyron_application)
target_include_directories(zyron_application PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_application
  PUBLIC  zyron::core zyron::audio zyron::library zyron::analysis zyron::recording zyron::stems zyron::ai
  PRIVATE zyron::juce_config juce::juce_events juce::juce_recommended_config_flags)
zyron_target_defaults(zyron_application)

juce_add_gui_app(ZYRON
  PRODUCT_NAME "ZYRON"
  COMPANY_NAME "ZYRON"
  VERSION "${PROJECT_VERSION}"
  ICON_BIG "${CMAKE_SOURCE_DIR}/resources/branding/zyron-icon.png"
  ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/branding/zyron-icon.png")

target_sources(ZYRON PRIVATE Main.cpp)

target_compile_definitions(ZYRON PRIVATE
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ZYRON,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ZYRON,JUCE_VERSION>")

target_link_libraries(ZYRON
  PRIVATE zyron::application zyron::core zyron::platform zyron::platform_juce zyron::ai zyron::audio zyron::midi zyron::ui
          zyron::juce_config juce::juce_recommended_config_flags
  PUBLIC  juce::juce_recommended_warning_flags)

zyron_target_defaults(ZYRON)

zyron_copy_onnx_runtime(ZYRON)

# vcpkg DLLs (SQLite) must sit next to the executable, otherwise Windows shows a "DLL not found" box and the app hangs
# at start-up. The same files go into the installer.
if(WIN32)
  add_custom_command(TARGET ZYRON POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:ZYRON>>,copy_if_different,true>
      $<TARGET_RUNTIME_DLLS:ZYRON>
      $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:ZYRON>>,$<TARGET_FILE_DIR:ZYRON>,>
    COMMAND_EXPAND_LISTS)
  install(FILES $<TARGET_RUNTIME_DLLS:ZYRON> DESTINATION bin COMPONENT Application OPTIONAL)
endif()
