# SPDX-License-Identifier: AGPL-3.0-only
zyron_require_catch2()
include(Catch)

# One test executable per module family; all of them are discovered by CTest.
function(zyron_add_test_executable name)
  add_executable(${name}
    support/NoCrashDialogs.cpp
    support/AllocationGuard.cpp
    support/AudioTestUtils.cpp
    ${ARGN})
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain)
  zyron_target_defaults(${name})
  if(WIN32)
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${name}>>,copy_if_different,true>
        $<TARGET_RUNTIME_DLLS:${name}>
        $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${name}>>,$<TARGET_FILE_DIR:${name}>,>
      COMMAND_EXPAND_LISTS)
  endif()
  catch_discover_tests(${name}
    DL_PATHS
      "$<TARGET_FILE_DIR:${name}>"
      "C:/Users/User/vcpkg/installed/x64-windows/debug/bin"
      "C:/Users/User/vcpkg/installed/x64-windows/bin")
endfunction()

zyron_add_test_executable(zyron_core_tests
  core/test_app_state.cpp
  core/test_command.cpp
  core/test_command_bus.cpp
  core/test_event_bus.cpp
  core/test_hardware.cpp
  core/test_waveform_data.cpp)
target_link_libraries(zyron_core_tests PRIVATE zyron::core)

# A system library that exists on every machine of the OS under test, for the dynamic-library loader tests.
if(WIN32)
  set(_zyron_test_library "kernel32.dll")
  set(_zyron_test_symbol "GetTickCount64")
  set(_zyron_test_nvml "nvml.dll")
elseif(APPLE)
  set(_zyron_test_library "libSystem.B.dylib")
  set(_zyron_test_symbol "strlen")
  set(_zyron_test_nvml "")
else()
  set(_zyron_test_library "libc.so.6")
  set(_zyron_test_symbol "strlen")
  set(_zyron_test_nvml "libnvidia-ml.so.1")
endif()

zyron_add_test_executable(zyron_platform_tests
  platform/test_dynamic_library.cpp
  platform/test_platform_interfaces.cpp)
target_link_libraries(zyron_platform_tests PRIVATE zyron::platform)
target_compile_definitions(zyron_platform_tests PRIVATE
  ZYRON_TEST_LIBRARY="${_zyron_test_library}"
  ZYRON_TEST_SYMBOL="${_zyron_test_symbol}"
  ZYRON_TEST_NVML_NAME="${_zyron_test_nvml}")

zyron_add_test_executable(zyron_audio_tests
  audio/test_audio_graph.cpp
  audio/test_audio_graph_integration.cpp
  audio/test_channel_strip.cpp
  audio/test_command_bridge.cpp
  audio/test_cue_loop_manager.cpp
  audio/test_cue_router.cpp
  audio/test_deck_player.cpp
  audio/test_effects.cpp
  audio/test_mixer.cpp
  audio/test_output_stage.cpp
  audio/test_stem_playback.cpp
  audio/test_stem_router.cpp
  audio/test_sync_manager.cpp
  audio/test_test_tone.cpp
  audio/test_track_loader.cpp
  support/test_rt_harness.cpp)
target_link_libraries(zyron_audio_tests PRIVATE zyron::dsp zyron::recording)

zyron_add_test_executable(zyron_ai_tests
  ai/test_nvml_gpu_probe.cpp
  ai/test_gpu_scheduler.cpp
  ai/test_model_manager.cpp
  ai/test_background_queue.cpp)
target_link_libraries(zyron_ai_tests PRIVATE zyron::ai zyron::platform)

zyron_add_test_executable(zyron_recording_tests recording/test_master_recorder.cpp)
target_link_libraries(zyron_recording_tests PRIVATE zyron::recording zyron::dsp)

zyron_add_test_executable(zyron_library_tests
  library/test_database.cpp
  library/test_scanner.cpp
  library/test_analysis_queue.cpp
  library/test_sqlite_library_source.cpp)
target_link_libraries(zyron_library_tests PRIVATE zyron::library)

zyron_add_test_executable(zyron_analysis_tests
  analysis/test_waveform_peaks.cpp
  analysis/test_features.cpp
  analysis/test_beat_detector.cpp
  analysis/test_beatgrid_editor.cpp
  analysis/test_key_energy.cpp)
target_link_libraries(zyron_analysis_tests PRIVATE zyron::analysis)

zyron_add_test_executable(zyron_stems_tests
  stems/test_stem_interfaces.cpp
  stems/test_demucs_separator.cpp
  stems/test_stem_cache.cpp)
target_link_libraries(zyron_stems_tests PRIVATE zyron::stems zyron::ai)

zyron_add_test_executable(zyron_ui_tests
  ui/test_waveform_view.cpp
  ui/test_library_component.cpp
  ui/test_two_deck_shell.cpp
  ui/test_model_manager_view.cpp
  ui/test_background_queue_view.cpp)
target_link_libraries(zyron_ui_tests PRIVATE zyron::ui)

zyron_add_test_executable(zyron_mvp1_gate_tests
  integration/test_mvp1_gate.cpp)
target_link_libraries(zyron_mvp1_gate_tests PRIVATE
  zyron::dsp
  zyron::recording
  zyron::library
  zyron::analysis
  zyron::stems
  zyron::ai)

# End-to-end smoke tests of the real binary (these exercise the JUCE probes, the engine and the UI, which have no
# unit tests). They need a display: on Linux they run under xvfb-run when it is installed, and are skipped otherwise.
if(TARGET ZYRON)
  set(_zyron_app_runner "")
  set(_zyron_can_run_app TRUE)
  if(UNIX AND NOT APPLE)
    find_program(ZYRON_XVFB_RUN xvfb-run)
    if(ZYRON_XVFB_RUN)
      set(_zyron_app_runner "${ZYRON_XVFB_RUN}" -a)
    else()
      set(_zyron_can_run_app FALSE)
      message(STATUS "xvfb-run not found: the ZYRON app smoke tests are not registered")
    endif()
  endif()

  if(_zyron_can_run_app)
    # Opens the window, runs the message loop once and exits 0.
    add_test(NAME app_smoke COMMAND ${_zyron_app_runner} $<TARGET_FILE:ZYRON> --smoke-test)
    # Prints the hardware report to stdout; the report header proves the probes ran.
    add_test(NAME app_print_hardware COMMAND ${_zyron_app_runner} $<TARGET_FILE:ZYRON> --print-hardware)
    set_tests_properties(app_print_hardware PROPERTIES PASS_REGULAR_EXPRESSION "ZYRON hardware report")
    set_tests_properties(app_smoke app_print_hardware PROPERTIES TIMEOUT 60)
  endif()
endif()
