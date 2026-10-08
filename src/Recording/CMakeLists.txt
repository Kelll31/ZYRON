# SPDX-License-Identifier: AGPL-3.0-only
#
# Recording: master tap, lock-free FIFO, background writer thread, file encoders (SPEC section 46).
add_library(zyron_recording STATIC
  MasterRecorder.cpp
  WavFileWriter.cpp)
add_library(zyron::recording ALIAS zyron_recording)

target_include_directories(zyron_recording PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_recording PUBLIC zyron::core)
zyron_target_defaults(zyron_recording)
